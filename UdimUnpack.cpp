// UdimUnpack.cpp : Defines the entry point for the application.
//

#include "UdimUnpack.h"
#include <string>
#include <regex>
#include <sstream>

static const FbxDouble kBoundaryTolerance = 0.001;
static const char* kMappingModeNames[] = { "None", "By Control Point", "By Polygon Vertex", "By Polygon", "By Edge", "All Same" };
static const char* kReferenceModeNames[] = { "Direct", "Index", "Index to Direct"};

// Max materials we can handle, including once we've generated new ones for UDIMs
#define MAX_MATERIAL_COUNT 128
#define MAX_UDIM_INDEX 100

// 2D array of udim versions of surface materials, first dimension in scene material order.
// Second dimension is the udim materials, indexed as udim-1001
static int udimMaterials[MAX_MATERIAL_COUNT][MAX_UDIM_INDEX];

// pair which maps the index of a material in a node to the index in the scene 
typedef std::pair<int, int> NodeToSceneMaterialIndex;

void InitUdimMaterials()
{
	// Would be nice to use designated initialisers
	for (int i = 0; i < MAX_MATERIAL_COUNT; ++i)
	{
		for (int j = 0; j < MAX_UDIM_INDEX; ++j)
		{
			udimMaterials[i][j] = -1;
		}
	}
}

bool NameHasUdimSuffix(const char* name)
{
	std::regex patt (".*_U1\\d\\d\\d");
	return std::regex_search(name, patt);
}

void ReplaceUdimSuffix(FbxSurfaceMaterial* mat, int udim)
{
	// this assumes the name already has the _U1xxx at the end
	std::string name = mat->GetName();
	name.replace(name.length() - 4, 4, std::to_string(udim));
	mat->SetName(name.c_str());	
}
void AddUdimSuffix(FbxSurfaceMaterial* mat, int udim)
{
	// this assumes the name DOES NOT have the _U1xxx at the end
	std::ostringstream ss;
	ss << mat->GetName() << "_U" << udim;
	mat->SetName(ss.str().c_str());	
}
// Convert a scene material index to an index which references the correct udim instance of that material
int GetUdimMaterialIndex(int matSceneIndex, int udim, FbxNode* node)
{
	int udimIndex = udim - 1001;
	int ret = udimMaterials[matSceneIndex][udimIndex];
	auto* scene = node->GetScene();

	if (ret == -1)
	{
		// Get the scene material, and put it in our udim 1001 slot (0)
		if (matSceneIndex >= scene->GetMaterialCount())
		{
			printf("ERROR: material index %d is out of range!\n", matSceneIndex);
			return -1;
		}
		
		// Use or Clone base mat
		// The first time we encounter the material it might not be at 1001
		// In fact there might not be a 1001
		// So just look for the _U1xxx suffix		
		auto* theMat = scene->GetMaterial(matSceneIndex);
		if (NameHasUdimSuffix(theMat->GetName()))
		{
			// OK we already used this in another UDIM, so clone it
			FbxSurfaceMaterial* newMat = static_cast<FbxSurfaceMaterial*>(theMat->Clone());
			ReplaceUdimSuffix(newMat, udim);
			// Cloning doesn't add the material to the scene, or node
			scene->AddMaterial(newMat);
			node->AddMaterial(newMat);
			ret = scene->GetMaterialCount() - 1;
			printf("Created material %s based on %s\n", newMat->GetName(), theMat->GetName());
			theMat = newMat;
		}
		else
		{
			// First use of this base material
			// Use this material in place but change its name
			AddUdimSuffix(theMat, udim);
			ret = matSceneIndex;
			printf("First UDIM material renamed to %s\n", theMat->GetName());
		}

		if (scene->GetMaterialCount() > MAX_MATERIAL_COUNT)
		{
			printf("ERROR: Creating materials for UDIMs has exceeded the number of allowed materials (%d)\n", MAX_MATERIAL_COUNT);
			exit(4);
		}

		// save for future reference
		udimMaterials[matSceneIndex][udimIndex] = ret;
	}
	else
	{
		// Make sure node references this material (may do already)
		FbxSurfaceMaterial* mat = scene->GetMaterial(ret);
		if (node->GetMaterialIndex(mat->GetName()) == -1)
		{
			node->AddMaterial(mat);
		}		
	}

	return ret;
	
}

int CalculateUdimPart(FbxDouble min, FbxDouble max)
{
	// We could judge tile solely by looking at minU / minV, but boundary conditions can make that unreliable
	// E.g. artist meant to put a vertex at U = 1.0 but actually it's at 0.999999
	// It appears that tooling that's aware of UDIMs can prevent this but let's be safe
	// So if close and maxU/V is over the threshold we round up.
	int iPart = static_cast<int>(min);
	if (static_cast<FbxDouble>(iPart) +1. - min <= kBoundaryTolerance && 
        max > iPart + 1.)
	{
		// minU very close to edge while maxU is over it, looks like a small error putting it on wrong side
		++iPart;
	}

	return iPart;
}
///  Calculate a UDIM tile (1001 etc) from UV range, or -1 if this isn't a UDIM (U or V range > 1)
int CalculateUdimTile(FbxDouble minU, FbxDouble minV, FbxDouble maxU, FbxDouble maxV)
{
	// UDIMs are laid out like this:
	//
	// 1031   1032  1033  1034 ... 1040
	// 1021   1022  1023  1024 ... 1030
	// 1011   1012  1013  1014 ... 1020
	// 1001   1002  1003  1004 ... 1010
	//
	// 1001 is U and V in (0,1)
	// 1002 is U in (1,2), V in (0,1)
	// 1011 is U in (0,1), V in (1,2) etc


	if ((maxU - minU) > (1 + kBoundaryTolerance) ||
        (maxV - minV) > (1 + kBoundaryTolerance))
	{
		// Not a UDIM tile if UV range is > 1
		return -1;
	}

	int uPart = CalculateUdimPart(minU, maxU);
	int vPart = CalculateUdimPart(minV, maxV);

	if (uPart > 9)
	{
		printf("Error: UV (%f,%f) is out of UDIM horizontal range\n", minU, minV);
		return -1;
	}
	
	return 1001 + vPart * 10 + uPart;
}

int GetSceneMaterialIndex(const FbxNode* node, int nodeMatIdx)
{
	FbxSurfaceMaterial* mat = node->GetMaterial(nodeMatIdx);
	FbxScene* scene = node->GetScene();
	// there is no GetMaterialIndex on FbxScene
	int sceneMatCount = scene->GetMaterialCount();
	for (int i = 0; i < sceneMatCount; ++i)
	{
		if (scene->GetMaterial(i) == mat)
		{
			return i;
		}
	}

	printf("ERROR: unable to find node material %s in scene\n", mat->GetName());
	return -1;
}

int GetSceneMaterialIndex(int nodeMatIdx, const NodeToSceneMaterialIndex* nodeToScene, int nodeToSceneCount)
{
	for (int i = 0; i < nodeToSceneCount; ++i)
	{
		if (nodeToScene[i].first == nodeMatIdx)
			return nodeToScene[i].second;
	}
	printf("ERROR: unable to find index in scene for node material index %d\n", nodeMatIdx);
	return -1;
}
int GetNodeMaterialIndex(int sceneMatIdx, const NodeToSceneMaterialIndex* nodeToScene, int nodeToSceneCount)
{
	for (int i = 0; i < nodeToSceneCount; ++i)
	{
		if (nodeToScene[i].second == sceneMatIdx)
			return nodeToScene[i].first;
	}
	printf("ERROR: unable to find node material index for scene material index %d\n", sceneMatIdx);
	return -1;
}

bool ProcessMeshNode(FbxNode* node)
{
	auto* mesh = node->GetMesh();
	bool anyChanges = false;

	// If no materials, nothing we can really do
	if (mesh->GetElementMaterialCount() == 0)
	{
		printf("WARNING: Skipping processing mesh '%s' because it has no materials assigned\n", mesh->GetName());
		return false;
	}
	if (mesh->GetElementMaterialCount() > 1)
	{
		printf("WARNING: Multiple sets of material assignments on mesh '%s'; only the first will be processed\n", mesh->GetName());
	}
	FbxGeometryElementMaterial* matElem = mesh->GetElementMaterial(0);

	// Lookup that's big enough to address all materials 
	NodeToSceneMaterialIndex nodeToSceneMatLookup[MAX_MATERIAL_COUNT];
	for (int i = 0; i < node->GetMaterialCount(); ++i)
	{
		nodeToSceneMatLookup[i].first = i;
		nodeToSceneMatLookup[i].second = GetSceneMaterialIndex(node, i);
	}
	int nodeToSceneMatCount = node->GetMaterialCount();

	// Figure out it we're dealing with a single material across the whole mesh or per polygon
	// If it's not by polygon, that's going to have to be changed if you split UDIMs into materials
	// But we'll wait until we see a UDIM > 1001 before doing that
	bool matByPolygon = matElem->GetMappingMode() == FbxGeometryElement::eByPolygon;

	FbxStringList uvSetNameList;
    mesh->GetUVSetNames(uvSetNameList);

	for (int i = 0; i < uvSetNameList.GetCount(); ++i)
	{
		const char* name = uvSetNameList.GetStringAt(i);
		auto* uvelem = mesh->GetElementUV(name);

        if (!uvelem)
            continue;

		if (uvelem->GetMappingMode() != FbxGeometryElement::eByControlPoint &&
			uvelem->GetMappingMode() != FbxGeometryElement::eByPolygonVertex)
		{
			continue;
		}

		const auto mapping = uvelem->GetMappingMode();
        if (mapping != FbxGeometryElement::eByPolygonVertex &&
           mapping != FbxGeometryElement::eByControlPoint )
            return false;

		const auto reference = uvelem->GetReferenceMode();

		//printf("UV set name found: %s Mapping mode: %s, Reference mode: %s\n", 
		//	name, 
		//	kMappingModeNames[mapping],
		//	kReferenceModeNames[reference]);

        //index array, where holds the index referenced to the uv data
        const bool useIndexes = reference != FbxGeometryElement::eDirect;
        const int indexCount= useIndexes ? uvelem->GetIndexArray().GetCount() : 0;
		
        //iterating through the data by polygon
        const int polyCount = mesh->GetPolygonCount();
		int baseIndex = 0;
        for( int p = 0; p < polyCount; ++p )
        {
            FbxDouble minU = std::numeric_limits<FbxDouble>::max();
            FbxDouble minV = std::numeric_limits<FbxDouble>::max();
            FbxDouble maxU = std::numeric_limits<FbxDouble>::min();
            FbxDouble maxV = std::numeric_limits<FbxDouble>::min();

            const int vertsPerPoly = mesh->GetPolygonSize(p);
            for( int v = 0; v < vertsPerPoly; ++v )
            {
            	int polyVert;

                //get the index of the current vertex in control points array
                if (uvelem->GetMappingMode() == FbxGeometryElement::eByControlPoint )
                {
                	polyVert = mesh->GetPolygonVertex(p, v);
                }
                else // (uvelem->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
                {
                	polyVert = baseIndex + v;
                	if (useIndexes && polyVert >= indexCount)
                		break;	
                }
            	//the UV index depends on the reference mode
                const int uvIndex = useIndexes ? uvelem->GetIndexArray().GetAt(polyVert) : polyVert;

                FbxVector2 uv = uvelem->GetDirectArray().GetAt(uvIndex);

                minU = std::min(minU, uv.mData[0]);
                minV = std::min(minV, uv.mData[1]);
                maxU = std::max(maxU, uv.mData[0]);
                maxV = std::max(maxV, uv.mData[1]);

            }
        	baseIndex += vertsPerPoly;

            const int udim = CalculateUdimTile(minU, minV, maxU, maxV);
        	if (udim == -1)
        		continue;
       		//printf("Poly %d UV range: (%f,%f)-(%f,%f) UDIM: %d\n", p, minU, minV, maxU, maxV, udim);
            if (udim > 1001)
            {
            	anyChanges = true;
            	// OK we need to reassign this polygon to a new material
            	if (!matByPolygon)
            	{
            		// Previously we've been using a global material for the whole mesh
            		// We need to alter material assignments to be by polygon now
            		// Init all single material index
            		int singleMatIdx = matElem->GetIndexArray().GetAt(0);
            		matElem->SetMappingMode(FbxGeometryElement::eByPolygon);
            		matElem->GetIndexArray().Resize(polyCount);
            		for (int mp = 0; mp < polyCount; ++mp)
            		{
            			matElem->GetIndexArray().SetAt(mp, singleMatIdx);
            		}
            		matByPolygon = true;
            	}
            }

            // Even for 1001 we'll do this mapping step to ensure our metadata is updated
            // But no new materials will be created

            int nodeMatIdx = matElem->GetIndexArray().GetAt(p);
            int sceneMatIdx = GetSceneMaterialIndex(nodeMatIdx, nodeToSceneMatLookup, nodeToSceneMatCount);
            int newSceneMatIdx = GetUdimMaterialIndex(sceneMatIdx, udim, node);
            if (node->GetMaterialCount() > nodeToSceneMatCount)
            {
            	// This means we added a new material, update the mapping
            	for (int mi = nodeToSceneMatCount; mi < node->GetMaterialCount(); ++mi)
            	{
            		nodeToSceneMatLookup[mi].first = mi;
            		nodeToSceneMatLookup[mi].second = GetSceneMaterialIndex(node, mi);
            	}
            	nodeToSceneMatCount = node->GetMaterialCount();
            }

            if (newSceneMatIdx != sceneMatIdx)
            {
            	int newNodeMatIdx = GetNodeMaterialIndex(newSceneMatIdx, nodeToSceneMatLookup, nodeToSceneMatCount);
            	// assign the new mat to node mat index
            	matElem->GetIndexArray().SetAt(p, newNodeMatIdx);
            	//printf("Poly %d assigned new material %s\n", p, node->GetMaterial(newNodeMatIdx)->GetName());
            }

            if (udim > 1001)
            {
            	// Fix UVs to be within the 0-1 range on this new material
            	for( int v = 0; v < vertsPerPoly; ++v )
            	{
            		int index = baseIndex + v;
            		if (useIndexes && index >= indexCount)
            			break;
            		int uvIndex = useIndexes ? uvelem->GetIndexArray().GetAt(index) : index;
            		FbxVector2 uv = uvelem->GetDirectArray().GetAt(uvIndex);
            		uv.mData[0] -= floor(uv.mData[0]);
            		uv.mData[1] -= floor(uv.mData[1]);
            		uvelem->GetDirectArray().SetAt(uvIndex, uv);
            	}
            }
        }
		
	}

	return anyChanges;
}

bool ScanNodesForMeshes(FbxNode* node)
{
    const char* nodeName = node->GetName();

	bool changes = false;
	
    // Print the node's attributes.
    for(int i = 0; i < node->GetNodeAttributeCount(); i++)
    {
    	auto* attrib = node->GetNodeAttributeByIndex(i);
    	if (attrib->GetAttributeType() == FbxNodeAttribute::eMesh)
    	{
    		printf("Found mesh node: '%s'\n", nodeName);
    		changes = ProcessMeshNode(node);
    	}
	    
    }

    // Recursively print the children.
    for(int i = 0; i < node->GetChildCount(); i++)
        changes = changes || ScanNodesForMeshes(node->GetChild(i));

	return changes;
}



struct Opts
{
	const char* inFilename;
	const char* outFilename;
	bool writeAlways;
	bool help;


	Opts()
		: inFilename(nullptr),
		  outFilename(nullptr),
		  writeAlways(false),
		  help(false)
	{
	}
};

Opts parseOpts(int argc, char** argv)
{
	Opts opts;
	// Yeah I could use a fancy options lib but pfft
	for (int i = 1; i < argc; ++i)
	{
		if (argv[i][0] == '-')
		{
			if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
				opts.help = true;
			else if (!strcmp(argv[i], "-a") || !strcmp(argv[i], "--always"))
				opts.writeAlways = true;
			else
				printf("WARNING: ignoring unknown argument '%s'\n", argv[i]);			
		}
		else
		{
			if (!opts.inFilename)
				opts.inFilename = argv[i];
			else if (!opts.outFilename)
				opts.outFilename = argv[i];
			else
				printf("WARNING: ignoring extra argument '%s'\n", argv[i]);			
		}
	}
	return opts;	
}

void printUsage(bool withHeader)
{
	if (withHeader)
		printf("UdimUnpack converts an FBX file using combined UDIM UVs to separate materials per UDIM\n\n");
	
	printf("Usage:\n");			
	printf("  UdimUnpack [options] <infilename.fbx> <outfilename.fbx>\n\n");			
	printf("Options:\n");
	printf("  -a, --always : Always write output file even if there were no changes needed\n");			
	printf("  --help       : Display this help\n\n");			
	
}

int main(int argc, char** argv)
{
	const Opts opts = parseOpts(argc, argv);

	if (opts.help)
	{
		printUsage(true);
		exit(0);
	}
	
	if (!opts.inFilename)
	{
		printf("Required: input FBX file name\n");
		printUsage(false);
        exit(-1);
	}
	if (!opts.outFilename)
	{
		printf("Required: output FBX file name\n");
		printUsage(false);
        exit(-1);
	}

	struct stat st;
	if (stat(opts.inFilename, &st) != 0)
	{
		printf("Input file %s not found\n", opts.inFilename);
		exit(-1);		
	}
	

	InitUdimMaterials();

	// Initialize the SDK manager. This object handles memory management.
    FbxManager* sdkManager = FbxManager::Create();
	
	// Create the IO settings object.
    FbxIOSettings *ios = FbxIOSettings::Create(sdkManager, IOSROOT);
    sdkManager->SetIOSettings(ios);

    // Create an importer using the SDK manager.
    FbxImporter* importer = FbxImporter::Create(sdkManager, "");

    // Use the first argument as the filename for the importer.
    if(!importer->Initialize(opts.inFilename, -1, sdkManager->GetIOSettings())) {
        printf("Call to FbxImporter::Initialize() failed.\n");
        printf("Error returned: %s\n\n", importer->GetStatus().GetErrorString());
        exit(-1);
    }


	// Create a new scene so that it can be populated by the imported file.
    FbxScene* scene = FbxScene::Create(sdkManager, "DummyScene");

    // Import the contents of the file into the scene.
    importer->Import(scene);
    printf("Imported scene OK.\n");

    // The file is imported, so get rid of the importer.
    importer->Destroy();

	int matCount = scene->GetMaterialCount();
	printf("Original materials: %d\n", matCount);	
	for (int i = 0; i < matCount; ++i)
	{
		auto* mat = scene->GetMaterial(i);
		printf("  %d: %s\n", i, mat->GetName());	
	}

	if (matCount > MAX_MATERIAL_COUNT)
	{
		printf("ERROR: too many materials, max allowed %d\n", MAX_MATERIAL_COUNT);
		exit(3);
	}

	// parse the scene looking for meshes
	const bool changed = ScanNodesForMeshes(scene->GetRootNode());

	if (changed || opts.writeAlways)
	{
		if (changed)
		{
			matCount = scene->GetMaterialCount();
			printf("New materials: %d\n", matCount);	
			for (int i = 0; i < matCount; ++i)
			{
				auto* mat = scene->GetMaterial(i);
				printf("  %d: %s\n", i, mat->GetName());	
			}
		}
		else
		{
			printf("No changes needed, but writing output anyway as requested.");			
		}
		

		printf("Exporting changes to %s\n", opts.outFilename);
	    FbxExporter* exporter = FbxExporter::Create(sdkManager, "");
	    bool exportStatus = exporter->Initialize(opts.outFilename, -1, sdkManager->GetIOSettings());
		if(!exportStatus) 
        {
	        printf("Call to FbxExporter::Initialize() failed.\n");
	        printf("Error returned: %s\n\n", exporter->GetStatus().GetErrorString());
		}
		else
		{
			exporter->Export(scene);
		}
		exporter->Destroy();
		printf("New mesh saved OK\n");
	}
	else
	{
		printf("No changes were made.\n");
	}

	
	sdkManager->Destroy();
	return 0;
}

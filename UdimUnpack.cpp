// UdimUnpack.cpp : Defines the entry point for the application.
//

#include "UdimUnpack.h"
#include <string>

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
// Convert a scene material index to an index which references the correct udim instance of that material
int GetUdimMaterialIndex(int matSceneIndex, int udim, FbxNode* node)
{
	int udimIndex = udim - 1001;
	int ret = udimMaterials[matSceneIndex][udimIndex];
	auto* scene = node->GetScene();

	if (ret == -1)
	{
		int baseMatIdx = udimMaterials[matSceneIndex][0];
		if (baseMatIdx == -1)
		{
			// Get the scene material, and put it in our udim 1001 slot (0)
			if (matSceneIndex >= scene->GetMaterialCount())
			{
				printf("ERROR: material index %d is out of range!", matSceneIndex);
				return -1;
			}
			// First UDIM always uses the main material
			udimMaterials[matSceneIndex][0] = matSceneIndex;
			baseMatIdx = matSceneIndex;
			// We'll also take this opportunity to change the name to include 1001 for clarity
			// since this *must* be the first time we don't have to check anything
			auto* baseMat = scene->GetMaterial(matSceneIndex);
			std::string newName(baseMat->GetName());
			newName.append("_1001");
			printf("Renaming material %s to %s\n", baseMat->GetName(), newName.c_str());
			baseMat->SetName(newName.c_str());
		}
		if (udimIndex == 0)
			ret = baseMatIdx;
		else
		{
			// Clone base mat
			// Put it in the right slot
			auto* baseMat = scene->GetMaterial(matSceneIndex);
			FbxSurfaceMaterial* newMat = static_cast<FbxSurfaceMaterial*>(baseMat->Clone());
			// Alter the name, will already have _1001 suffix since based on root udim
			std::string name = newMat->GetName();
			name.replace(name.length() - 4, 4, std::to_string(udim));
			newMat->SetName(name.c_str());
			// Cloning doesn't add the material to the scene, or node
			scene->AddMaterial(newMat);
			node->AddMaterial(newMat);
			ret = scene->GetMaterialCount() - 1;
			printf("Created material %s based on %s\n", newMat->GetName(), baseMat->GetName());

			if (scene->GetMaterialCount() > MAX_MATERIAL_COUNT)
			{
				printf("ERROR: Creating materials for UDIMs has exceeded the number of allowed materials (%d)", MAX_MATERIAL_COUNT);
				exit(4);
			}
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
		printf("Error: UV (%f,%f) is out of UDIM horizontal range", minU, minV);
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

	printf("ERROR: unable to find node material %s in scene", mat->GetName());
	return -1;
}

int GetSceneMaterialIndex(int nodeMatIdx, const NodeToSceneMaterialIndex* nodeToScene, int nodeToSceneCount)
{
	for (int i = 0; i < nodeToSceneCount; ++i)
	{
		if (nodeToScene[i].first == nodeMatIdx)
			return nodeToScene[i].second;
	}
	printf("ERROR: unable to find index in scene for node material index %d", nodeMatIdx);
	return -1;
}
int GetNodeMaterialIndex(int sceneMatIdx, const NodeToSceneMaterialIndex* nodeToScene, int nodeToSceneCount)
{
	for (int i = 0; i < nodeToSceneCount; ++i)
	{
		if (nodeToScene[i].second == sceneMatIdx)
			return nodeToScene[i].first;
	}
	printf("ERROR: unable to find node material index for scene material index %d", sceneMatIdx);
	return -1;
}

bool ProcessMeshNode(FbxNode* node)
{
	auto* mesh = node->GetMesh();
	auto* scene = node->GetScene();
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
	int nodeToSceneMatCount = 0; // actively used count of above

	for (int i = 0; i < node->GetMaterialCount(); ++i)
	{
		nodeToSceneMatLookup[i].first = i;
		nodeToSceneMatLookup[i].second = GetSceneMaterialIndex(node, i);
	}
	nodeToSceneMatCount = node->GetMaterialCount();

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

        if(!uvelem)
            continue;

		const auto mapping = uvelem->GetMappingMode();
        if(mapping != FbxGeometryElement::eByPolygonVertex &&
           mapping != FbxGeometryElement::eByControlPoint )
            return false;

		const auto reference = uvelem->GetReferenceMode();

		printf("UV set name found: %s Mapping mode: %s, Reference mode: %s\n", 
			name, 
			kMappingModeNames[mapping],
			kReferenceModeNames[reference]);

        //index array, where holds the index referenced to the uv data
        const bool useIndexes = uvelem->GetReferenceMode() != FbxGeometryElement::eDirect;
        const int indexCount= useIndexes ? uvelem->GetIndexArray().GetCount() : 0;
		
        //iterating through the data by polygon
        const int polyCount = mesh->GetPolygonCount();

        if(uvelem->GetMappingMode() == FbxGeometryElement::eByControlPoint )
        {
            for(int p = 0; p < polyCount; ++p )
            {
            	FbxDouble minU = std::numeric_limits<FbxDouble>::max();
            	FbxDouble minV = std::numeric_limits<FbxDouble>::max();
            	FbxDouble maxU = std::numeric_limits<FbxDouble>::min();
            	FbxDouble maxV = std::numeric_limits<FbxDouble>::min();
            	
                const int vertsPerPoly = mesh->GetPolygonSize(p);
                for(int v = 0; v < vertsPerPoly; ++v )
                {
                    FbxVector2 uv;

                    //get the index of the current vertex in control points array
                    int lPolyVertIndex = mesh->GetPolygonVertex(p, v);

                    //the UV index depends on the reference mode
                    int uvIndex = useIndexes ? uvelem->GetIndexArray().GetAt(lPolyVertIndex) : lPolyVertIndex;

                    uv = uvelem->GetDirectArray().GetAt(uvIndex);

                	minU = std::min(minU, uv.mData[0]);
                	minV = std::min(minV, uv.mData[1]);
                	maxU = std::max(maxU, uv.mData[0]);
                	maxV = std::max(maxV, uv.mData[1]);
                }

            	const int udim = CalculateUdimTile(minU, minV, maxU, maxV);
                printf("Poly %d UV range: (%f,%f)-(%f,%f) UDIM: %d\n", p, minU, minV, maxU, maxV, udim);
            	
            }
        }
        else if (uvelem->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
        {
            for( int p = 0; p < polyCount; ++p )
            {
            	FbxDouble minU = std::numeric_limits<FbxDouble>::max();
            	FbxDouble minV = std::numeric_limits<FbxDouble>::max();
            	FbxDouble maxU = std::numeric_limits<FbxDouble>::min();
            	FbxDouble maxV = std::numeric_limits<FbxDouble>::min();

            	const int vertsPerPoly = mesh->GetPolygonSize(p);
            	int baseIndex = p * vertsPerPoly;
                for( int v = 0; v < vertsPerPoly; ++v )
                {
                	int index = baseIndex + v;
                	if (useIndexes && index >= indexCount)
                        break;
                	
                    //the UV index depends on the reference mode
                    int uvIndex = useIndexes ? uvelem->GetIndexArray().GetAt(index) : index;

                    FbxVector2 uv = uvelem->GetDirectArray().GetAt(uvIndex);

                	minU = std::min(minU, uv.mData[0]);
                	minV = std::min(minV, uv.mData[1]);
                	maxU = std::max(maxU, uv.mData[0]);
                	maxV = std::max(maxV, uv.mData[1]);

                }

            	const int udim = CalculateUdimTile(minU, minV, maxU, maxV);
                printf("Poly %d UV range: (%f,%f)-(%f,%f) UDIM: %d\n", p, minU, minV, maxU, maxV, udim);
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
            		int newNodeMatIdx = GetNodeMaterialIndex(newSceneMatIdx, nodeToSceneMatLookup, nodeToSceneMatCount);
            		// assign the new mat to node mat index
            		matElem->GetIndexArray().SetAt(p, newNodeMatIdx);
            		printf("Poly %d assigned new material %s\n", p, node->GetMaterial(newNodeMatIdx)->GetName());

            		// Now fix UVs to be within the 0-1 range on this new material
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



int main(int argc, char** argv)
{
	if (argc < 2)
	{
		printf("Required: input FBX file name");
        exit(-1);
	}
	if (argc < 3)
	{
		printf("Required: output FBX file name");
        exit(-1);
	}
	const char* filename = argv[1];
	const char* outfilename = argv[2];

	InitUdimMaterials();

	// Initialize the SDK manager. This object handles memory management.
    FbxManager* sdkManager = FbxManager::Create();
	
	// Create the IO settings object.
    FbxIOSettings *ios = FbxIOSettings::Create(sdkManager, IOSROOT);
    sdkManager->SetIOSettings(ios);

    // Create an importer using the SDK manager.
    FbxImporter* importer = FbxImporter::Create(sdkManager, "");

    // Use the first argument as the filename for the importer.
    if(!importer->Initialize(filename, -1, sdkManager->GetIOSettings())) {
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
		printf("ERROR: too many materials, max allowed %d", MAX_MATERIAL_COUNT);
		exit(3);
	}

	// parse the scene looking for meshes
	const bool changed = ScanNodesForMeshes(scene->GetRootNode());

	if (changed)
	{
		matCount = scene->GetMaterialCount();
		printf("New materials: %d\n", matCount);	
		for (int i = 0; i < matCount; ++i)
		{
			auto* mat = scene->GetMaterial(i);
			printf("  %d: %s\n", i, mat->GetName());	
		}

		printf("Exporting changes to %s\n", outfilename);
	    FbxExporter* exporter = FbxExporter::Create(sdkManager, "");
	    bool exportStatus = exporter->Initialize(outfilename, -1, sdkManager->GetIOSettings());
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
		printf("New mesh saved OK");
	}
	else
	{
		printf("No changes were made.");
	}

	
	sdkManager->Destroy();
	return 0;
}

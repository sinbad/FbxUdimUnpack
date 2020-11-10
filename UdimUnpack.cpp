// UdimUnpack.cpp : Defines the entry point for the application.
//

#include "UdimUnpack.h"

using namespace std;


static const FbxDouble boundaryTolerance = 0.001;


int CalculateUdimPart(FbxDouble min, FbxDouble max)
{
	// We could judge tile solely by looking at minU / minV, but boundary conditions can make that unreliable
	// E.g. artist meant to put a vertex at U = 1.0 but actually it's at 0.999999
	// It appears that tooling that's aware of UDIMs can prevent this but let's be safe
	// So if close and maxU/V is over the threshold we round up.
	int iPart = static_cast<int>(min);
	if (static_cast<FbxDouble>(iPart+1) - min <= boundaryTolerance && 
        max > iPart + 1)
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


	if ((maxU - minU) > (1 + boundaryTolerance) ||
        (maxV - minV) > (1 + boundaryTolerance))
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

bool ProcessMeshNode(FbxNode* node)
{
	auto* mesh = node->GetMesh();

    // Materials are at the NODE level, not at the mesh level (weird)
	int matCount = node->GetMaterialCount();
	printf("%d materials found\n", matCount);

	for (int i = 0; i < matCount; ++i)
	{
		FbxSurfaceMaterial* mat = node->GetMaterial(i);
		printf("Material %d: '%s'\n", i, mat->GetName());
		
	}


	FbxStringList uvSetNameList;
    mesh->GetUVSetNames(uvSetNameList);

	for (int i = 0; i < uvSetNameList.GetCount(); ++i)
	{
		const char* name = uvSetNameList.GetStringAt(i);
        auto* elem = mesh->GetElementUV(name);

        if(!elem)
            continue;

		const auto mapping = elem->GetMappingMode();
        if(mapping != FbxGeometryElement::eByPolygonVertex &&
           mapping != FbxGeometryElement::eByControlPoint )
            return false;

		const auto reference = elem->GetReferenceMode();

		printf("UV set name found: %s Mapping mode: %s, Reference mode: %s\n", 
			name, 
			mapping == FbxGeometryElement::eByPolygonVertex ? "Vertex" : "Control Point",
			reference == FbxGeometryElement::eDirect ? "Direct" : "Indexed");



        //index array, where holds the index referenced to the uv data
        const bool useIndexes = elem->GetReferenceMode() != FbxGeometryElement::eDirect;
        const int indexCount= useIndexes ? elem->GetIndexArray().GetCount() : 0;
		
        //iterating through the data by polygon
        const int polyCount = mesh->GetPolygonCount();

        if(elem->GetMappingMode() == FbxGeometryElement::eByControlPoint )
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
                    int uvIndex = useIndexes ? elem->GetIndexArray().GetAt(lPolyVertIndex) : lPolyVertIndex;

                    uv = elem->GetDirectArray().GetAt(uvIndex);

                	minU = std::min(minU, uv.mData[0]);
                	minV = std::min(minV, uv.mData[1]);
                	maxU = std::max(maxU, uv.mData[0]);
                	maxV = std::max(maxV, uv.mData[1]);
                }

            	const int udim = CalculateUdimTile(minU, minV, maxU, maxV);
                printf("Poly %d UV range: (%f,%f)-(%f,%f) UDIM: %d\n", p, minU, minV, maxU, maxV, udim);
            	
            }
        }
        else if (elem->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
        {
            int polyIndexCounter = 0;
            for( int p = 0; p < polyCount; ++p )
            {
            	FbxDouble minU = std::numeric_limits<FbxDouble>::max();
            	FbxDouble minV = std::numeric_limits<FbxDouble>::max();
            	FbxDouble maxU = std::numeric_limits<FbxDouble>::min();
            	FbxDouble maxV = std::numeric_limits<FbxDouble>::min();

            	const int vertsPerPoly = mesh->GetPolygonSize(p);
                for( int v = 0; v < vertsPerPoly; ++v )
                {
                	if (useIndexes && polyIndexCounter >= indexCount)
                        break;
                	
                    FbxVector2 uv;

                    //the UV index depends on the reference mode
                    int uvIndex = useIndexes ? elem->GetIndexArray().GetAt(polyIndexCounter) : polyIndexCounter;

                    uv = elem->GetDirectArray().GetAt(uvIndex);

                	minU = std::min(minU, uv.mData[0]);
                	minV = std::min(minV, uv.mData[1]);
                	maxU = std::max(maxU, uv.mData[0]);
                	maxV = std::max(maxV, uv.mData[1]);

                	++polyIndexCounter;
                }

            	const int udim = CalculateUdimTile(minU, minV, maxU, maxV);
                printf("Poly %d UV range: (%f,%f)-(%f,%f) UDIM: %d\n", p, minU, minV, maxU, maxV, udim);
            }
        }
		
	}

	return false;
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

	// parse the scene looking for meshes
	const bool changed = ScanNodesForMeshes(scene->GetRootNode());

	if (changed)
	{
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
	}
	else
	{
		printf("No changes were made.");
	}

	
	sdkManager->Destroy();
	return 0;
}

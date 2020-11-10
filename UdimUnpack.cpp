// UdimUnpack.cpp : Defines the entry point for the application.
//

#include "UdimUnpack.h"

using namespace std;


bool ProcessMeshNode(FbxNode* node)
{
	auto* mesh = node->GetMesh();

    // Materials are at the NODE level, not at the mesh level (weird)
	int matCount = node->GetMaterialCount();
	printf("%d materials found\n", matCount);

	for (int i = 0; i < matCount; ++i)
	{
		auto* mat = node->GetMaterial(i);
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
                printf("Poly: %d\n", p);
                const int vertsPerPoly = mesh->GetPolygonSize(p);
                for(int v = 0; v < vertsPerPoly; ++v )
                {
                    FbxVector2 uv;

                    //get the index of the current vertex in control points array
                    int lPolyVertIndex = mesh->GetPolygonVertex(p, v);

                    //the UV index depends on the reference mode
                    int uvIndex = useIndexes ? elem->GetIndexArray().GetAt(lPolyVertIndex) : lPolyVertIndex;

                    uv = elem->GetDirectArray().GetAt(uvIndex);

                	printf("UV: (%f,%f)\t(Index: %d)\n", uv.mData[0], uv.mData[1], uvIndex);

                }
            }
        }
        else if (elem->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
        {
            int polyIndexCounter = 0;
            for( int p = 0; p < polyCount; ++p )
            {
                printf("Poly: %d\n", p);
                const int vertsPerPoly = mesh->GetPolygonSize(p);
                for( int v = 0; v < vertsPerPoly; ++v )
                {
                	if (useIndexes && polyIndexCounter >= indexCount)
                        break;
                	
                    FbxVector2 uv;

                    //the UV index depends on the reference mode
                    int uvIndex = useIndexes ? elem->GetIndexArray().GetAt(polyIndexCounter) : polyIndexCounter;

                    uv = elem->GetDirectArray().GetAt(uvIndex);

                	printf("UV: (%f,%f)\t(Index: %d)\n", uv.mData[0], uv.mData[1], uvIndex);

                	++polyIndexCounter;
                }
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

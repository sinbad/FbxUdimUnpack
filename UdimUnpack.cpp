// UdimUnpack.cpp : Defines the entry point for the application.
//

#include "UdimUnpack.h"


using namespace std;

int main()
{
	cout << "Hello CMake." << endl;


	// Initialize the SDK manager. This object handles memory management.
    FbxManager* lSdkManager = FbxManager::Create();
	
	return 0;
}

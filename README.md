es-sync
=======

MongoDB to ElasticSearch binary

Compiling
---------

### Dependencies
- boost >= 1.51
- libmongoclient 2.4.x
- libcurl with at least HTTP and HTTPS support

### Building
In both cases, boost and mongo are static builds. Libcurl is built statically on linux only. It comes with a huge list of dependencies, so its built with the bare minimum requirements :
    

#### Linux (full static build)
    g++ -o coorp_es_sync_linux_x64 coorp_es_sync.cpp -Wno-write-strings <include path list> <libraries search list> -static-libgcc -lmongoclient -lboost_thread-mt-sd -lboost_program_options-mt-sd -lboost_filesystem-mt-sd -lboost_system-mt-sd -lboost_regex-mt-sd libcurl.a -Wl,-static -lssl -lcrypto -lpthread -lz -ldl -lrt
Then strip the binary if necessary to reduce its size.

#### Mac OS
The mac os build only requires a partial static build, and relies on system libraries to be dynamically linked (curl, openssl, ...)

    clang++ -mmacosx-version-min=10.6 -o coorp_es_sync_darwin_x64 coorp_es_sync.cpp -Wno-write-strings <include path list> <libraries search list> -lmongoclient -lboost_thread-mt-s -lboost_program_options-mt-s -lboost_filesystem-mt-s -lboost_system-mt-s -lboost_regex-mt-sd -lcurl

Node.js wrapper usage
---------------------

The wrapper exports 2 functions, sync_full and sync_incremental.
Sample usage :

    require('coorp-es-sync').sync_full(function(error, stdout, stderr) {
        console.log(stdout);
        if (error == null)
            console.log("All was good");
        else
            console.log("Batch failed");
    });

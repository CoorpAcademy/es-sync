es-sync
=======

[![Greenkeeper badge](https://badges.greenkeeper.io/CoorpAcademy/es-sync.svg?token=eef4be83f1cd79ab83851cf14b70c105e63531c54c480c1ff32aa48f32ad4af5)](https://greenkeeper.io/)

MongoDB to ElasticSearch binary

Compiling
---------

### Dependencies
- boost >= 1.51
- libmongoclient 2.4.x
- libcurl with at least HTTP and HTTPS support

### Building
In both cases, boost and mongo are static builds. Libcurl is built statically on linux only. It comes with a huge list of dependencies, so its built with the bare minimum requirements :

    ./configure --disable-shared --enable-static --without-gnutls --without-polarssl --without-cyassl --without-nss --without-axtls --without-libmetalink --without-libssh2 --without-librtmp --without-librtmp --without-libidn --without-nghttp2 --disable-ldap --disable-ldaps --disable-rtsp --disable-proxy --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-sspi --disable-crypto-auth --disable-ntlm-wb --disable-tls-srp --without-zlib --disable-ftp

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

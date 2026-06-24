Module["FS_readdir"] = FS.readdir;
Module["FS_rmdir"] = FS.rmdir;
Module["FS_analyzePath"] = FS.analyzePath;
Module["ready"] = readyPromise;

if (typeof globalThis !== "undefined") {
    globalThis["NEEDLE:USD:GET"] = getUsdModule;
}

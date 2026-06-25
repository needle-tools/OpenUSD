Module["FS_readdir"] = FS.readdir;
Module["FS_rmdir"] = FS.rmdir;
Module["FS_analyzePath"] = FS.analyzePath;
Module["ready"] = Promise.resolve(Module);

if (typeof globalThis !== "undefined") {
    globalThis["NEEDLE:USD:GET"] = getUsdModule;
}

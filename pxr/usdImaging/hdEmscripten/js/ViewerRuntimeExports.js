Module["FS_readdir"] = FS.readdir;
Module["FS_rmdir"] = FS.rmdir;
Module["FS_analyzePath"] = FS.analyzePath;
Module["FS_readFile"] = FS.readFile;
Module["ready"] = Promise.resolve(Module);

// Bindings marked async() in Embind return a Promise in every runtime.
// Asyncify returns one only when the call suspended, and both runtimes throw
// argument errors before native code runs. Settle every result and error
// through the Promise so the API does not depend on the runtime.
{
    const craftEmbindInvoker = craftInvokerFunction;
    craftInvokerFunction = function (humanName, argTypes, classType, cppInvokerFunc, cppTargetFunc, isAsync) {
        const invoker = craftEmbindInvoker(humanName, argTypes, classType, cppInvokerFunc, cppTargetFunc, isAsync);
        if (!isAsync) return invoker;
        return function (...args) {
            try { return Promise.resolve(invoker.apply(this, args)); }
            catch (error) { return Promise.reject(error); }
        };
    };
}

// HydraDriver instances come only from CreateHydraDriver. Embind exports
// every registered class; remove this one so the module has no constructor
// that cannot work. Instances keep their prototype.
{
    const previousOnRuntimeInitialized = Module["onRuntimeInitialized"];
    Module["onRuntimeInitialized"] = function () {
        delete Module["HydraDriver"];
        previousOnRuntimeInitialized?.call(Module);
    };
}

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

// Embind constructors cannot be async, so `new HdWebSyncDriver` returns the
// Promise of CreateHdWebSyncDriver. The alias keeps the native prototype, so
// instanceof and prototype extensions still work.
{
    const previousOnRuntimeInitialized = Module["onRuntimeInitialized"];
    Module["onRuntimeInitialized"] = function () {
        const NativeDriver = Module["HdWebSyncDriver"];
        function HdWebSyncDriver(delegate, file) {
            if (!new.target) throw new TypeError("Use new HdWebSyncDriver or CreateHdWebSyncDriver");
            return Module["CreateHdWebSyncDriver"](delegate, file);
        }
        Object.setPrototypeOf(HdWebSyncDriver, NativeDriver);
        HdWebSyncDriver.prototype = NativeDriver.prototype;
        Module["HdWebSyncDriver"] = HdWebSyncDriver;
        previousOnRuntimeInitialized?.call(Module);
    };
}

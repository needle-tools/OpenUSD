// JSPI always returns a Promise from suspension-capable entry points. Keep
// the constructor alias for callers that already await remote constructors,
// including its prototype so instanceof and prototype extensions still work.
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
    Module["asyncRuntime"] = "jspi";
    previousOnRuntimeInitialized?.call(Module);
};

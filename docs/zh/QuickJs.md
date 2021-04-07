# QuickJs 引擎

目前支持的QuickJs引擎版本为2021-03-27，其他版本理论上也能支持。

说明：
由于QuickJs的C-API比较受限，ScriptX将部分需要的能力通过JS来实现。

但是仍然有部分能力（如弱引用）在JS中也不受支持。这个情况下你需要QuickJs打一个ScriptX提供的补丁包[backend/QuickJs/patch](../../backend/QuickJs/patch)，或直接使用笔者的 [fork](https://github.com/LanderlYoung/quickjs/tree/58ac957eee57e301ed0cc52b5de5495a7e1c1827) 。

目前这个补丁仅影响 `script::Local<T>` 的功能。
即使不打该补丁包，也仅仅是 `script::Local<T>` 表现为强引用即`script::Global<T>`，除此之外无差别。
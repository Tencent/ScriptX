# QuickJs engine

Current support QuickJs version is 2021-03-27.

Other version should be also supported.

Note:

QuickJs's C-API is limited, so ScriptX has workaround it mostly be using JS functions.

But some of them are not available in JS, like WeakRef. In such case you may want to apply a patch file provided by ScriptX in [backend/QuickJs/patch](../../backend/QuickJs/patch), or just use the [fork](https://github.com/LanderlYoung/quickjs/tree/58ac957eee57e301ed0cc52b5de5495a7e1c1827) by the author.

Currently the patch is only needed when you need the `script::Local<T>` to work as expected. Otherwise the `script::Local<T>` would behave like `script::Global<T>`.
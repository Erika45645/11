/*
 * Copyright (c) 2023-present Samsung Electronics Co., Ltd
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 *  USA
 */

#include <jni.h>

#include <EscargotPublic.h>
#include <cassert>

using namespace Escargot;

#if defined(ANDROID)
#include <android/log.h>
#define  LOG_TAG    "Escargot"
#define  LOGUNK(...)  __android_log_print(ANDROID_LOG_UNKNOWN,LOG_TAG,__VA_ARGS__)
#define  LOGDEF(...)  __android_log_print(ANDROID_LOG_DEFAULT,LOG_TAG,__VA_ARGS__)
#define  LOGV(...)  __android_log_print(ANDROID_LOG_VERBOSE,LOG_TAG,__VA_ARGS__)
#define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG,__VA_ARGS__)
#define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define  LOGW(...)  __android_log_print(ANDROID_LOG_WARN,LOG_TAG,__VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)
#define  LOGF(...)  __android_log_print(ANDROID_FATAL_ERROR,LOG_TAG,__VA_ARGS__)
#define  LOGS(...)  __android_log_print(ANDROID_SILENT_ERROR,LOG_TAG,__VA_ARGS__)
#else
#define  LOGUNK(...)  fprintf(stdout,__VA_ARGS__)
#define  LOGDEF(...)  fprintf(stdout,__VA_ARGS__)
#define  LOGV(...)  fprintf(stdout,__VA_ARGS__)
#define  LOGD(...)  fprintf(stdout,__VA_ARGS__)
#define  LOGI(...)  fprintf(stdout,__VA_ARGS__)
#define  LOGW(...)  fprintf(stdout,__VA_ARGS__)
#define  LOGE(...)  fprintf(stderr,__VA_ARGS__)
#define  LOGF(...)  fprintf(stderr,__VA_ARGS__)
#define  LOGS(...)  fprintf(stderr,__VA_ARGS__)
#endif

#define THROW_NPE_RETURN_NULL(param, paramType)                                                      \
    if (env->ExceptionCheck()) {                                                                     \
        return 0;                                                                                    \
    }                                                                                                \
    if (!param) {                                                                                    \
        env->ThrowNew(env->FindClass("java/lang/NullPointerException"), paramType" cannot be null"); \
        return 0;                                                                                    \
    }

static JavaVM* g_jvm;
static size_t g_nonPointerValueLast = reinterpret_cast<size_t>(ValueRef::createUndefined());
static jobject createJavaObjectFromValue(JNIEnv* env, ValueRef* value);
static ValueRef* unwrapValueRefFromValue(JNIEnv* env, jclass clazz, jobject object);
static OptionalRef<JNIEnv> fetchJNIEnvFromCallback()
{
    JNIEnv* env = nullptr;
    if (g_jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) == JNI_EDETACHED) {
#if defined(_JAVASOFT_JNI_H_) // oraclejdk or openjdk
        if (g_jvm->AttachCurrentThread(reinterpret_cast<void **>(&env), NULL) != 0) {
#else
        if (g_jvm->AttachCurrentThread(reinterpret_cast<JNIEnv **>(&env), NULL) != 0) {
#endif
            // give up
            return nullptr;
        }
    }
    return env;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_samsung_lwe_escargot_Escargot_init(JNIEnv* env, jclass clazz)
{
    thread_local static bool inited = false;
    if (!inited) {
        if (!g_jvm) {
            env->GetJavaVM(&g_jvm);
        }
        inited = true;
    }
}

static bool stringEndsWith(const std::string& str, const std::string& suffix)
{
    return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}

static const char32_t offsetsFromUTF8[6] = { 0x00000000UL, 0x00003080UL, 0x000E2080UL, 0x03C82080UL, static_cast<char32_t>(0xFA082080UL), static_cast<char32_t>(0x82082080UL) };

static char32_t readUTF8Sequence(const char*& sequence, bool& valid, int& charlen)
{
    unsigned length;
    const char sch = *sequence;
    valid = true;
    if ((sch & 0x80) == 0)
        length = 1;
    else {
        unsigned char ch2 = static_cast<unsigned char>(*(sequence + 1));
        if ((sch & 0xE0) == 0xC0
            && (ch2 & 0xC0) == 0x80)
            length = 2;
        else {
            unsigned char ch3 = static_cast<unsigned char>(*(sequence + 2));
            if ((sch & 0xF0) == 0xE0
                && (ch2 & 0xC0) == 0x80
                && (ch3 & 0xC0) == 0x80)
                length = 3;
            else {
                unsigned char ch4 = static_cast<unsigned char>(*(sequence + 3));
                if ((sch & 0xF8) == 0xF0
                    && (ch2 & 0xC0) == 0x80
                    && (ch3 & 0xC0) == 0x80
                    && (ch4 & 0xC0) == 0x80)
                    length = 4;
                else {
                    valid = false;
                    sequence++;
                    return -1;
                }
            }
        }
    }

    charlen = length;
    char32_t ch = 0;
    switch (length) {
        case 4:
            ch += static_cast<unsigned char>(*sequence++);
            ch <<= 6; // Fall through.
        case 3:
            ch += static_cast<unsigned char>(*sequence++);
            ch <<= 6; // Fall through.
        case 2:
            ch += static_cast<unsigned char>(*sequence++);
            ch <<= 6; // Fall through.
        case 1:
            ch += static_cast<unsigned char>(*sequence++);
    }
    return ch - offsetsFromUTF8[length - 1];
}

static OptionalRef<StringRef> builtinHelperFileRead(OptionalRef<ExecutionStateRef> state, const char* fileName, const char* builtinName)
{
    FILE* fp = fopen(fileName, "r");
    if (fp) {
        StringRef* src = StringRef::emptyString();
        std::string utf8Str;
        std::basic_string<unsigned char, std::char_traits<unsigned char>> str;
        char buf[512];
        bool hasNonLatin1Content = false;
        size_t readLen;
        while ((readLen = fread(buf, 1, sizeof buf, fp))) {
            if (!hasNonLatin1Content) {
                const char* source = buf;
                int charlen;
                bool valid;
                while (source < buf + readLen) {
                    char32_t ch = readUTF8Sequence(source, valid, charlen);
                    if (ch > 255) {
                        hasNonLatin1Content = true;
                        fseek(fp, 0, SEEK_SET);
                        break;
                    } else {
                        str += (unsigned char)ch;
                    }
                }
            } else {
                utf8Str.append(buf, readLen);
            }
        }
        fclose(fp);
        if (StringRef::isCompressibleStringEnabled()) {
            if (state) {
                if (hasNonLatin1Content) {
                    src = StringRef::createFromUTF8ToCompressibleString(state->context()->vmInstance(), utf8Str.data(), utf8Str.length(), false);
                } else {
                    src = StringRef::createFromLatin1ToCompressibleString(state->context()->vmInstance(), str.data(), str.length());
                }
            } else {
                if (hasNonLatin1Content) {
                    src = StringRef::createFromUTF8(utf8Str.data(), utf8Str.length(), false);
                } else {
                    src = StringRef::createFromLatin1(str.data(), str.length());
                }
            }
        } else {
            if (hasNonLatin1Content) {
                src = StringRef::createFromUTF8(utf8Str.data(), utf8Str.length(), false);
            } else {
                src = StringRef::createFromLatin1(str.data(), str.length());
            }
        }
        return src;
    } else {
        char msg[1024];
        snprintf(msg, sizeof(msg), "GlobalObject.%s: cannot open file %s", builtinName, fileName);
        if (state) {
            state->throwException(URIErrorObjectRef::create(state.get(), StringRef::createFromUTF8(msg, strnlen(msg, sizeof msg))));
        } else {
            LOGD("%s", msg);
        }
        return nullptr;
    }
}

class ShellPlatform : public PlatformRef {
public:
    bool m_canBlock;

    ShellPlatform()
            : m_canBlock(true)
    {
    }

    void setCanBlock(bool b)
    {
        m_canBlock = b;
    }

    virtual void markJSJobEnqueued(ContextRef* relatedContext) override
    {
        // ignore. we always check pending job after eval script
    }

    virtual void markJSJobFromAnotherThreadExists(ContextRef* relatedContext) override
    {
        // ignore. we always check pending job after eval script
    }

    virtual LoadModuleResult onLoadModule(ContextRef* relatedContext, ScriptRef* whereRequestFrom, StringRef* moduleSrc, ModuleType type) override
    {
        std::string referrerPath = whereRequestFrom->src()->toStdUTF8String();
        auto& loadedModules = *reinterpret_cast<std::vector<std::tuple<std::string, ContextRef*, PersistentRefHolder<ScriptRef>>>*>(threadLocalCustomData());

        for (size_t i = 0; i < loadedModules.size(); i++) {
            if (std::get<2>(loadedModules[i]) == whereRequestFrom) {
                referrerPath = std::get<0>(loadedModules[i]);
                break;
            }
        }

        std::string absPath = absolutePath(referrerPath, moduleSrc->toStdUTF8String());
        if (absPath.length() == 0) {
            std::string s = "Error reading : " + moduleSrc->toStdUTF8String();
            return LoadModuleResult(ErrorObjectRef::Code::None, StringRef::createFromUTF8(s.data(), s.length()));
        }

        for (size_t i = 0; i < loadedModules.size(); i++) {
            if (std::get<0>(loadedModules[i]) == absPath && std::get<1>(loadedModules[i]) == relatedContext) {
                return LoadModuleResult(std::get<2>(loadedModules[i]));
            }
        }

        OptionalRef<StringRef> source = builtinHelperFileRead(nullptr, absPath.data(), "");
        if (!source) {
            std::string s = "Error reading : " + absPath;
            return LoadModuleResult(ErrorObjectRef::Code::None, StringRef::createFromUTF8(s.data(), s.length()));
        }

        ScriptParserRef::InitializeScriptResult parseResult;
        StringRef* srcName = StringRef::createFromUTF8(absPath.data(), absPath.size());

        if (type == ModuleJSON) {
            parseResult = relatedContext->scriptParser()->initializeJSONModule(source.value(), srcName);
        } else {
            parseResult = relatedContext->scriptParser()->initializeScript(source.value(), srcName, true);
        }

        if (!parseResult.isSuccessful()) {
            return LoadModuleResult(parseResult.parseErrorCode, parseResult.parseErrorMessage);
        }

        return LoadModuleResult(parseResult.script.get());
    }

    virtual void didLoadModule(ContextRef* relatedContext, OptionalRef<ScriptRef> referrer, ScriptRef* loadedModule) override
    {
        std::string path;
        if (referrer && loadedModule->src()->length() && loadedModule->src()->charAt(0) != '/') {
            path = absolutePath(referrer->src()->toStdUTF8String(), loadedModule->src()->toStdUTF8String());
        } else {
            path = absolutePath(loadedModule->src()->toStdUTF8String());
        }
        auto& loadedModules = *reinterpret_cast<std::vector<std::tuple<std::string, ContextRef*, PersistentRefHolder<ScriptRef>>>*>(threadLocalCustomData());
        loadedModules.push_back(std::make_tuple(path, relatedContext, PersistentRefHolder<ScriptRef>(loadedModule)));
    }

    virtual void hostImportModuleDynamically(ContextRef* relatedContext, ScriptRef* referrer, StringRef* src, ModuleType type, PromiseObjectRef* promise) override
    {
        LoadModuleResult loadedModuleResult = onLoadModule(relatedContext, referrer, src, type);
        notifyHostImportModuleDynamicallyResult(relatedContext, referrer, src, promise, loadedModuleResult);
    }

    virtual bool canBlockExecution(ContextRef* relatedContext) override
    {
        return m_canBlock;
    }

    virtual void* allocateThreadLocalCustomData() override
    {
        return new std::vector<std::tuple<std::string /* abs path */, ContextRef*, PersistentRefHolder<ScriptRef>>>();
    }

    virtual void deallocateThreadLocalCustomData() override
    {
        delete reinterpret_cast<std::vector<std::tuple<std::string, ContextRef*, PersistentRefHolder<ScriptRef>>>*>(threadLocalCustomData());
    }

private:
    std::string dirnameOf(const std::string& fname)
    {
        size_t pos = fname.find_last_of("/");
        if (std::string::npos == pos) {
            pos = fname.find_last_of("\\/");
        }
        return (std::string::npos == pos)
               ? ""
               : fname.substr(0, pos);
    }

    std::string absolutePath(const std::string& referrerPath, const std::string& src)
    {
        std::string utf8MayRelativePath = dirnameOf(referrerPath) + "/" + src;
        auto absPath = realpath(utf8MayRelativePath.data(), nullptr);
        if (!absPath) {
            return std::string();
        }
        std::string utf8AbsolutePath = absPath;
        free(absPath);

        return utf8AbsolutePath;
    }

    std::string absolutePath(const std::string& src)
    {
        auto absPath = realpath(src.data(), nullptr);
        if (!absPath) {
            return std::string();
        }
        std::string utf8AbsolutePath = absPath;
        free(absPath);

        return utf8AbsolutePath;
    }
};

static Evaluator::EvaluatorResult evalScript(ContextRef* context, StringRef* source, StringRef* srcName, bool shouldPrintScriptResult, bool isModule)
{
    if (stringEndsWith(srcName->toStdUTF8String(), "mjs")) {
        isModule = isModule || true;
    }

    auto scriptInitializeResult = context->scriptParser()->initializeScript(source, srcName, isModule);
    if (!scriptInitializeResult.script) {
        LOGD("Script parsing error: ");
        switch (scriptInitializeResult.parseErrorCode) {
            case Escargot::ErrorObjectRef::Code::SyntaxError:
                LOGD("SyntaxError");
                break;
            case Escargot::ErrorObjectRef::Code::EvalError:
                LOGD("EvalError");
                break;
            case Escargot::ErrorObjectRef::Code::RangeError:
                LOGD("RangeError");
                break;
            case Escargot::ErrorObjectRef::Code::ReferenceError:
                LOGD("ReferenceError");
                break;
            case Escargot::ErrorObjectRef::Code::TypeError:
                LOGD("TypeError");
                break;
            case Escargot::ErrorObjectRef::Code::URIError:
                LOGD("URIError");
                break;
            default:
                break;
        }
        LOGD(": %s\n", scriptInitializeResult.parseErrorMessage->toStdUTF8String().data());
        Evaluator::EvaluatorResult evalResult;
        evalResult.error = StringRef::createFromASCII("script parsing error");
        return evalResult;
    }

    auto evalResult = Evaluator::execute(context, [](ExecutionStateRef* state, ScriptRef* script) -> ValueRef* {
                                             return script->execute(state);
                                         },
                                         scriptInitializeResult.script.get());

    if (!evalResult.isSuccessful()) {
        LOGD("Uncaught %s:\n", evalResult.resultOrErrorToString(context)->toStdUTF8String().data());
        for (size_t i = 0; i < evalResult.stackTrace.size(); i++) {
            LOGD("%s (%d:%d)\n", evalResult.stackTrace[i].srcName->toStdUTF8String().data(), (int)evalResult.stackTrace[i].loc.line, (int)evalResult.stackTrace[i].loc.column);
        }
        return evalResult;
    }

    if (shouldPrintScriptResult) {
        LOGD("%s", evalResult.resultOrErrorToString(context)->toStdUTF8String().data());
    }

    bool result = true;
    while (context->vmInstance()->hasPendingJob() || context->vmInstance()->hasPendingJobFromAnotherThread()) {
        if (context->vmInstance()->waitEventFromAnotherThread(10)) {
            context->vmInstance()->executePendingJobFromAnotherThread();
        }
        if (context->vmInstance()->hasPendingJob()) {
            auto jobResult = context->vmInstance()->executePendingJob();
            if (shouldPrintScriptResult || jobResult.error) {
                if (jobResult.error) {
                    LOGD("Uncaught %s:(in promise job)\n", jobResult.resultOrErrorToString(context)->toStdUTF8String().data());
                    result = false;
                } else {
                    LOGD("%s(in promise job)\n", jobResult.resultOrErrorToString(context)->toStdUTF8String().data());
                }
            }
        }
    }
    return evalResult;
}


extern "C"
JNIEXPORT void JNICALL
Java_com_samsung_lwe_escargot_NativePointerHolder_releaseNativePointerMemory(JNIEnv* env,
                                                                             jclass clazz,
                                                                             jlong pointer)
{
    uint64_t ptrInNumber = (uint64_t)(pointer);
    if (ptrInNumber > g_nonPointerValueLast && !(pointer & 1)) {
        PersistentRefHolder<void>* pRef = reinterpret_cast<PersistentRefHolder<void>*>(pointer);
        delete pRef;
    }
}

static void gcCallback(void* data)
{
    auto env = fetchJNIEnvFromCallback();
    if (!env) {
        LOGE("failed to fetch env from gc event callback");
        return;
    }
    if (!env->ExceptionCheck()) {
        env->PushLocalFrame(32);
        jclass clazz = env->FindClass("com/samsung/lwe/escargot/NativePointerHolder");
        jmethodID mId = env->GetStaticMethodID(clazz, "cleanUp", "()V");
        env->CallStaticVoidMethod(clazz, mId);
        env->PopLocalFrame(NULL);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_samsung_lwe_escargot_Globals_initializeGlobals(JNIEnv* env, jclass clazz)
{
    if (!Globals::isInitialized()) {
        Globals::initialize(new ShellPlatform());
        Memory::addGCEventListener(Memory::MARK_START, gcCallback, nullptr);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_samsung_lwe_escargot_Globals_finalizeGlobals(JNIEnv* env, jclass clazz)
{
    if (Globals::isInitialized()) {
        // java object cleanup
        gcCallback(nullptr);

        Memory::removeGCEventListener(Memory::MARK_START, gcCallback, nullptr);
        Globals::finalize();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_samsung_lwe_escargot_Globals_initializeThread(JNIEnv* env, jclass clazz)
{
    if (!Globals::isInitialized()) {
       Globals::initializeThread();
    }
}
extern "C"
JNIEXPORT void JNICALL
Java_com_samsung_lwe_escargot_Globals_finalizeThread(JNIEnv* env, jclass clazz)
{
    if (Globals::isInitialized()) {
        Globals::finalizeThread();
    }
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_Globals_isInitialized(JNIEnv* env, jclass clazz)
{
    return Globals::isInitialized();
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_samsung_lwe_escargot_Globals_version(JNIEnv* env, jclass clazz)
{
    std::string version = Globals::version();
    std::basic_string<uint16_t> u16Version;

    for (auto c: version) {
        u16Version.push_back(c);
    }

    return env->NewString(u16Version.data(), u16Version.length());
}
extern "C"
JNIEXPORT jstring JNICALL
Java_com_samsung_lwe_escargot_Globals_buildDate(JNIEnv* env, jclass clazz)
{
    std::string version = Globals::buildDate();
    std::basic_string<uint16_t> u16Version;

    for (auto c: version) {
        u16Version.push_back(c);
    }

    return env->NewString(u16Version.data(), u16Version.length());
}
extern "C"
JNIEXPORT void JNICALL
Java_com_samsung_lwe_escargot_Memory_gc(JNIEnv* env, jclass clazz)
{
    Memory::gc();
}
extern "C"
JNIEXPORT jlong JNICALL
Java_com_samsung_lwe_escargot_Memory_heapSize(JNIEnv* env, jclass clazz)
{
    return Memory::heapSize();
}
extern "C"
JNIEXPORT jlong JNICALL
Java_com_samsung_lwe_escargot_Memory_totalSize(JNIEnv* env, jclass clazz)
{
    return Memory::totalSize();
}
extern "C"
JNIEXPORT void JNICALL
Java_com_samsung_lwe_escargot_Memory_setGCFrequency(JNIEnv* env, jclass clazz, jint value)
{
    Memory::setGCFrequency(value);
}

static std::string fetchStringFromJavaOptionalString(JNIEnv *env, jobject optional)
{
    auto classOptionalString = env->GetObjectClass(optional);
    auto methodIsPresent = env->GetMethodID(classOptionalString, "isPresent", "()Z");
    if (env->CallBooleanMethod(optional, methodIsPresent)) {
        auto methodGet = env->GetMethodID(classOptionalString, "get", "()Ljava/lang/Object;");
        jboolean isSucceed;
        jstring value = static_cast<jstring>(env->CallObjectMethod(optional, methodGet));
        const char* str = env->GetStringUTFChars(
                value, &isSucceed);
        auto length = env->GetStringUTFLength(value);
        auto ret = std::string(str, length);
        env->ReleaseStringUTFChars(value, str);
        return ret;
    }
    return std::string();
}

template<typename T>
static jobject nativeOptionalValueIntoJavaOptionalValue(JNIEnv* env, OptionalRef<T> ref)
{
    if (env->ExceptionCheck()) {
        return nullptr;
    }
    jclass optionalClazz = env->FindClass("java/util/Optional");
    if (ref) {
        jmethodID ctorMethod = env->GetStaticMethodID(optionalClazz, "of",
                                                      "(Ljava/lang/Object;)Ljava/util/Optional;");
        return env->CallStaticObjectMethod(optionalClazz, ctorMethod, createJavaObjectFromValue(env, ref.value()));
    }

    return env->CallStaticObjectMethod(optionalClazz, env->GetStaticMethodID(optionalClazz, "empty",
                                                                             "()Ljava/util/Optional;"));
}

template<typename NativeType>
static PersistentRefHolder<NativeType>* getPersistentPointerFromJava(JNIEnv *env, jclass clazz, jobject object)
{
    auto ptr = env->GetLongField(object, env->GetFieldID(clazz, "m_nativePointer", "J"));
    PersistentRefHolder<NativeType>* pVMRef = reinterpret_cast<PersistentRefHolder<NativeType>*>(ptr);
    return pVMRef;
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_VMInstance_create(JNIEnv *env, jclass clazz, jobject locale,
                                                jobject timezone)
{
    THROW_NPE_RETURN_NULL(locale, "Optional<String>");
    THROW_NPE_RETURN_NULL(timezone, "Optional<String>");

    std::string localeString = fetchStringFromJavaOptionalString(env, locale);
    std::string timezoneString = fetchStringFromJavaOptionalString(env, timezone);

    auto vmRef = VMInstanceRef::create(localeString.length() ? localeString.data() : nullptr,
                                       timezoneString.length() ? timezoneString.data() : nullptr);
    PersistentRefHolder<VMInstanceRef>* pRef = new PersistentRefHolder<VMInstanceRef>(std::move(vmRef));
    return env->NewObject(clazz, env->GetMethodID(clazz, "<init>", "(J)V"), reinterpret_cast<jlong>(pRef));
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_VMInstance_hasPendingJob(JNIEnv* env, jobject thiz)
{
    auto vmPtr = getPersistentPointerFromJava<VMInstanceRef>(env, env->GetObjectClass(thiz),
                                                             thiz);
    return vmPtr->get()->hasPendingJob();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_samsung_lwe_escargot_VMInstance_executePendingJob(JNIEnv* env, jobject thiz)
{
    auto vmPtr = getPersistentPointerFromJava<VMInstanceRef>(env, env->GetObjectClass(thiz),
                                                             thiz);
    if (vmPtr->get()->hasPendingJob()) {
        vmPtr->get()->executePendingJob();
    }
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_Context_create(JNIEnv* env, jclass clazz, jobject vmInstance)
{
    THROW_NPE_RETURN_NULL(vmInstance, "VMInstance");

    auto vmPtr = getPersistentPointerFromJava<VMInstanceRef>(env, env->GetObjectClass(vmInstance),
                                                             vmInstance);
    auto contextRef = ContextRef::create(vmPtr->get());
    PersistentRefHolder<ContextRef>* pRef = new PersistentRefHolder<ContextRef>(std::move(contextRef));
    return env->NewObject(clazz, env->GetMethodID(clazz, "<init>", "(J)V"), reinterpret_cast<jlong>(pRef));
}

static StringRef* createJSStringFromJava(JNIEnv* env, jstring str)
{
    if (!str) {
        return StringRef::emptyString();
    }
    jboolean isSucceed;
    const char* cString = env->GetStringUTFChars(str, &isSucceed);
    StringRef* code = StringRef::createFromUTF8(cString, env->GetStringUTFLength(str));
    env->ReleaseStringUTFChars(str, cString);
    return code;
}

static jstring createJavaStringFromJS(JNIEnv* env, StringRef* string)
{
    std::basic_string<uint16_t> buf;
    auto bad = string->stringBufferAccessData();
    buf.reserve(bad.length);

    for (size_t i = 0; i < bad.length ; i ++) {
        buf.push_back(bad.charAt(i));
    }

    return env->NewString(buf.data(), buf.length());
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_Evaluator_evalScript(JNIEnv* env, jclass clazz, jobject context,
                                                   jstring source, jstring sourceFileName,
                                                   jboolean shouldPrintScriptResult)
{
    THROW_NPE_RETURN_NULL(context, "Context");

    auto ptr = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    Evaluator::EvaluatorResult result = evalScript(ptr->get(), createJSStringFromJava(env, source),
                             createJSStringFromJava(env, sourceFileName), shouldPrintScriptResult,
                             false);
    if (env->ExceptionCheck()) {
        return nullptr;
    }
    jclass optionalClazz = env->FindClass("java/util/Optional");
    if (result.isSuccessful()) {
        return env->CallStaticObjectMethod(optionalClazz,
                                           env->GetStaticMethodID(optionalClazz, "of",
                                                                  "(Ljava/lang/Object;)Ljava/util/Optional;"),
                                           createJavaObjectFromValue(env, result.result));
    }
    return env->CallStaticObjectMethod(optionalClazz, env->GetStaticMethodID(optionalClazz, "empty",
                                                                             "()Ljava/util/Optional;"));
}

static void throwJavaRuntimeException(ExecutionStateRef* state)
{
    state->throwException(ErrorObjectRef::create(state, ErrorObjectRef::None, StringRef::createFromASCII("Java runtime exception")));
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_Bridge_register(JNIEnv* env, jclass clazz, jobject context,
                                              jstring objectName, jstring propertyName,
                                              jobject adapter)
{
    THROW_NPE_RETURN_NULL(context, "Context");
    THROW_NPE_RETURN_NULL(adapter, "Adapter");

    auto contextPtr = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context),
                                                               context);
    auto jsObjectName = createJSStringFromJava(env, objectName);
    auto jsPropertyName = createJSStringFromJava(env, propertyName);

    if (!jsObjectName->length() || !jsPropertyName->length()) {
        return 0;
    }

    adapter = env->NewGlobalRef(adapter);

    auto evalResult = Evaluator::execute(contextPtr->get(),
                                         [](ExecutionStateRef* state, JNIEnv* env, jobject adapter,
                                            StringRef* jsObjectName,
                                            StringRef* jsPropertyName) -> ValueRef* {
                                             auto globalObject = state->context()->globalObject();
                                             ObjectRef* targetObject;

                                             ValueRef* willBeTargetObject = globalObject->getOwnProperty(
                                                     state, jsObjectName);
                                             if (willBeTargetObject->isObject()) {
                                                 targetObject = willBeTargetObject->asObject();
                                             } else {
                                                 targetObject = ObjectRef::create(state);
                                                 globalObject->defineDataProperty(state,
                                                                                  jsObjectName,
                                                                                  targetObject,
                                                                                  true, true, true);
                                             }

                                             FunctionObjectRef::NativeFunctionInfo info(
                                                     AtomicStringRef::emptyAtomicString(),
                                                     [](ExecutionStateRef* state,
                                                        ValueRef* thisValue, size_t argc,
                                                        ValueRef** argv,
                                                        bool isConstructorCall) -> ValueRef* {
                                                         FunctionObjectRef* callee = state->resolveCallee().get();
                                                         jobject jo = static_cast<jobject>(reinterpret_cast<FunctionObjectRef*>(callee)->extraData());
                                                         auto env = fetchJNIEnvFromCallback();
                                                         if (!env) {
                                                             // give up
                                                             LOGE("could not fetch env from callback");
                                                             return ValueRef::createUndefined();
                                                         }

                                                         if (env->ExceptionCheck()) {
                                                             throwJavaRuntimeException(state);
                                                             return ValueRef::createUndefined();
                                                         }

                                                         env->PushLocalFrame(32);

                                                         jobject callbackArg;
                                                         jclass optionalClazz = env->FindClass(
                                                                 "java/util/Optional");
                                                         if (argc) {
                                                             callbackArg = env->CallStaticObjectMethod(
                                                                     optionalClazz,
                                                                     env->GetStaticMethodID(
                                                                             optionalClazz, "of",
                                                                             "(Ljava/lang/Object;)Ljava/util/Optional;"),
                                                                     createJavaObjectFromValue(env.get(), argv[0]));
                                                         } else {
                                                             callbackArg = env->CallStaticObjectMethod(
                                                                     optionalClazz,
                                                                     env->GetStaticMethodID(
                                                                             optionalClazz, "empty",
                                                                             "()Ljava/util/Optional;"));
                                                         }
                                                         auto javaReturnValue = env->CallObjectMethod(
                                                                 jo,
                                                                 env->GetMethodID(
                                                                         env->GetObjectClass(jo),
                                                                         "callback",
                                                                         "(Ljava/util/Optional;)Ljava/util/Optional;"),
                                                                 callbackArg);

                                                         if (env->ExceptionCheck()) {
                                                             env->PopLocalFrame(NULL);
                                                             throwJavaRuntimeException(state);
                                                             return ValueRef::createUndefined();
                                                         }

                                                         auto methodIsPresent = env->GetMethodID(
                                                                 optionalClazz, "isPresent", "()Z");
                                                         ValueRef* nativeReturnValue = ValueRef::createUndefined();
                                                         if (javaReturnValue && env->CallBooleanMethod(javaReturnValue,
                                                                                    methodIsPresent)) {
                                                             auto methodGet = env->GetMethodID(
                                                                     optionalClazz, "get",
                                                                     "()Ljava/lang/Object;");
                                                             jobject value = env->CallObjectMethod(
                                                                     javaReturnValue, methodGet);
                                                             nativeReturnValue = unwrapValueRefFromValue(
                                                                     env.get(),
                                                                     env->GetObjectClass(value),
                                                                     value);
                                                         }

                                                         env->PopLocalFrame(NULL);
                                                         return nativeReturnValue;
                                                     }, 1, true, false);
                                             FunctionObjectRef* callback = FunctionObjectRef::create(
                                                     state, info);
                                             targetObject->defineDataProperty(state, jsPropertyName,
                                                                              callback, true, true,
                                                                              true);
                                             return callback;
                                         }, env, adapter, jsObjectName, jsPropertyName);

    if (evalResult.isSuccessful()) {
        FunctionObjectRef* callback = evalResult.result->asFunctionObject();
        callback->setExtraData(adapter);
        Memory::gcRegisterFinalizer(callback, [](void* self) {
            jobject jo = static_cast<jobject>(reinterpret_cast<FunctionObjectRef*>(self)->extraData());
            auto env = fetchJNIEnvFromCallback();
            if (env) {
                env->DeleteGlobalRef(jo);
            }
        });
    } else {
        env->DeleteGlobalRef(adapter);
    }

    return evalResult.isSuccessful();
}

static jobject createJavaValueObject(JNIEnv* env, jclass clazz, ValueRef* value)
{
    jobject valueObject;
    if (!value->isStoredInHeap()) {
        valueObject = env->NewObject(clazz, env->GetMethodID(clazz, "<init>", "(JZ)V"), reinterpret_cast<jlong>(value), jboolean(false));
    } else {
        PersistentRefHolder<ValueRef>* pRef = new PersistentRefHolder<ValueRef>(value);
        jlong ptr = reinterpret_cast<size_t>(pRef);
        valueObject = env->NewObject(clazz, env->GetMethodID(clazz, "<init>", "(J)V"), ptr);
    }
    return valueObject;
}

static jobject createJavaValueObject(JNIEnv* env, const char* className, ValueRef* value)
{
    return createJavaValueObject(env, env->FindClass(className), value);
}

static ValueRef* unwrapValueRefFromValue(JNIEnv* env, jclass clazz, jobject object)
{
    auto ptr = env->GetLongField(object, env->GetFieldID(clazz, "m_nativePointer", "J"));
    if (static_cast<size_t>(ptr) <= g_nonPointerValueLast || (static_cast<size_t>(ptr) & 1)) {
        return reinterpret_cast<ValueRef*>(ptr);
    } else {
        PersistentRefHolder<ValueRef>* ref = reinterpret_cast<PersistentRefHolder<ValueRef>*>(ptr);
        return ref->get();
    }
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_createUndefined(JNIEnv* env, jclass clazz)
{
    return createJavaValueObject(env, clazz, ValueRef::createUndefined());
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_createNull(JNIEnv* env, jclass clazz)
{
    return createJavaValueObject(env, clazz, ValueRef::createNull());
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_create__I(JNIEnv* env, jclass clazz, jint value)
{
    return createJavaValueObject(env, clazz, ValueRef::create(value));
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_create__D(JNIEnv* env, jclass clazz, jdouble value)
{
    return createJavaValueObject(env, clazz, ValueRef::create(value));
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_create__Z(JNIEnv* env, jclass clazz, jboolean value)
{
    return createJavaValueObject(env, clazz, ValueRef::create(static_cast<bool>(value)));
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_create__Ljava_util_Optional_2(JNIEnv* env,
                                                                            jclass clazz,
                                                                            jobject value)
{
    OptionalRef<StringRef> descString;
    if (value) {
        auto classOptionalJavaScriptString = env->GetObjectClass(value);
        auto methodIsPresent = env->GetMethodID(classOptionalJavaScriptString, "isPresent", "()Z");
        if (env->CallBooleanMethod(value, methodIsPresent)) {
            auto methodGet = env->GetMethodID(classOptionalJavaScriptString, "get", "()Ljava/lang/Object;");
            jboolean isSucceed;
            jobject javaObjectValue = env->CallObjectMethod(value, methodGet);
            descString = unwrapValueRefFromValue(env, env->GetObjectClass(javaObjectValue), javaObjectValue)->asString();
        }
    }

    return createJavaValueObject(env, "com/samsung/lwe/escargot/JavaScriptSymbol", SymbolRef::create(descString));
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_create__Ljava_lang_String_2(JNIEnv* env, jclass clazz,
                                                                          jstring value)
{
    return createJavaValueObject(env, "com/samsung/lwe/escargot/JavaScriptString", createJSStringFromJava(env, value));
}

static jobject createJavaObjectFromValue(JNIEnv* env, ValueRef* value)
{
    if (!value->isStoredInHeap() || value->isNumber()) {
        return createJavaValueObject(env, "com/samsung/lwe/escargot/JavaScriptValue", value);
    } else if (value->isString()) {
        return createJavaValueObject(env, "com/samsung/lwe/escargot/JavaScriptString", value);
    } else if (value->isSymbol()) {
        return createJavaValueObject(env, "com/samsung/lwe/escargot/JavaScriptSymbol", value);
    } else if (value->isBigInt()) {
        return createJavaValueObject(env, "com/samsung/lwe/escargot/JavaScriptBigInt", value);
    } else if (value->isObject()) {
        if (value->isArrayObject()) {
            return createJavaValueObject(env, "com/samsung/lwe/escargot/JavaScriptArrayObject", value);
        } else if (value->isGlobalObject()) {
            return createJavaValueObject(env, "com/samsung/lwe/escargot/JavaScriptGlobalObject", value);
        } else if (value->isFunctionObject()) {
            if (value->asFunctionObject()->extraData()) {
                return createJavaValueObject(env, "com/samsung/lwe/escargot/JavaScriptJavaCallbackFunctionObject", value);
            }
            return createJavaValueObject(env, "com/samsung/lwe/escargot/JavaScriptFunctionObject", value);
        } else {
            return createJavaValueObject(env, "com/samsung/lwe/escargot/JavaScriptObject", value);
        }
    } else {
        abort();
    }
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_isUndefined(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->isUndefined();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_isNull(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->isNull();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_isUndefinedOrNull(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->isUndefinedOrNull();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_isNumber(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->isNumber();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_isInt32(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->isInt32();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_isBoolean(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->isBoolean();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_isTrue(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->isTrue();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_isFalse(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->isFalse();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_isString(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->isString();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_isSymbol(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->isSymbol();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_isBigInt(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->isBigInt();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_isCallable(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->isCallable();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_isObject(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->isObject();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_isArrayObject(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->isArrayObject();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_isFunctionObject(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->isFunctionObject();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_asBoolean(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->asBoolean();
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_asInt32(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->asInt32();
}

extern "C"
JNIEXPORT jdouble JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_asNumber(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->asNumber();
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_asScriptString(JNIEnv* env, jobject thiz)
{
    return thiz;
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_asScriptSymbol(JNIEnv* env, jobject thiz)
{
    return thiz;
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_asScriptBigInt(JNIEnv* env, jobject thiz)
{
    return thiz;
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_asScriptObject(JNIEnv* env, jobject thiz)
{
    return thiz;
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_asScriptArrayObject(JNIEnv* env, jobject thiz)
{
    return thiz;
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_asScriptFunctionObject(JNIEnv* env, jobject thiz)
{
    return thiz;
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_samsung_lwe_escargot_JavaScriptString_toJavaString(JNIEnv* env, jobject thiz)
{
    StringRef* string = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->asString();
    return createJavaStringFromJS(env, string);
}

static jobject storeExceptionOnContextAndReturnsIt(JNIEnv* env, jobject contextObject, ContextRef* context, Evaluator::EvaluatorResult& evaluatorResult)
{
    if (env->ExceptionCheck()) {
        return nullptr;
    }
    jclass optionalClazz = env->FindClass("java/util/Optional");
    // store exception to context
    auto fieldId = env->GetFieldID(env->GetObjectClass(contextObject), "m_lastThrownException", "Ljava/util/Optional;");
    auto fieldValue = env->CallStaticObjectMethod(optionalClazz,
                                                  env->GetStaticMethodID(optionalClazz, "of",
                                                                         "(Ljava/lang/Object;)Ljava/util/Optional;"),
                                                  createJavaObjectFromValue(env, evaluatorResult.error.value()));
    env->SetObjectField(contextObject, fieldId, fieldValue);

    return env->CallStaticObjectMethod(optionalClazz, env->GetStaticMethodID(optionalClazz, "empty",
                                                                             "()Ljava/util/Optional;"));
}

static jobject createOptionalValueFromEvaluatorJavaScriptValueResult(JNIEnv* env, jobject contextObject, ContextRef* context, Evaluator::EvaluatorResult& evaluatorResult)
{
    if (env->ExceptionCheck()) {
        return nullptr;
    }
    if (evaluatorResult.isSuccessful()) {
        jclass optionalClazz = env->FindClass("java/util/Optional");
        return env->CallStaticObjectMethod(optionalClazz,
                                           env->GetStaticMethodID(optionalClazz, "of",
                                                                  "(Ljava/lang/Object;)Ljava/util/Optional;"),
                                           createJavaObjectFromValue(env, evaluatorResult.result));
    }

    return storeExceptionOnContextAndReturnsIt(env, contextObject, context, evaluatorResult);
}

static jobject createOptionalValueFromEvaluatorBooleanResult(JNIEnv* env, jobject contextObject, ContextRef* context, Evaluator::EvaluatorResult& evaluatorResult)
{
    if (env->ExceptionCheck()) {
        return nullptr;
    }
    if (evaluatorResult.isSuccessful()) {
        jclass optionalClazz = env->FindClass("java/util/Optional");
        auto booleanClazz = env->FindClass("java/lang/Boolean");
        auto valueOfMethodId = env->GetStaticMethodID(booleanClazz, "valueOf", "(Z)Ljava/lang/Boolean;");
        auto javaBoolean = env->CallStaticObjectMethod(booleanClazz, valueOfMethodId, (jboolean)evaluatorResult.result->asBoolean());
        return env->CallStaticObjectMethod(optionalClazz,
                                           env->GetStaticMethodID(optionalClazz, "of",
                                                                  "(Ljava/lang/Object;)Ljava/util/Optional;"),
                                           javaBoolean);
    }

    return storeExceptionOnContextAndReturnsIt(env, contextObject, context, evaluatorResult);
}

static jobject createOptionalValueFromEvaluatorIntegerResult(JNIEnv* env, jobject contextObject, ContextRef* context, Evaluator::EvaluatorResult& evaluatorResult)
{
    if (env->ExceptionCheck()) {
        return nullptr;
    }
    if (evaluatorResult.isSuccessful()) {
        jclass optionalClazz = env->FindClass("java/util/Optional");
        auto containerClass = env->FindClass("java/lang/Integer");
        auto valueOfMethodId = env->GetStaticMethodID(containerClass, "valueOf", "(I)Ljava/lang/Integer;");
        auto javaValue = env->CallStaticObjectMethod(containerClass, valueOfMethodId, (jint)evaluatorResult.result->asInt32());
        return env->CallStaticObjectMethod(optionalClazz,
                                           env->GetStaticMethodID(optionalClazz, "of",
                                                                  "(Ljava/lang/Object;)Ljava/util/Optional;"),
                                           javaValue);
    }

    return storeExceptionOnContextAndReturnsIt(env, contextObject, context, evaluatorResult);
}

static jobject createOptionalValueFromEvaluatorDoubleResult(JNIEnv* env, jobject contextObject, ContextRef* context, Evaluator::EvaluatorResult& evaluatorResult)
{
    if (env->ExceptionCheck()) {
        return nullptr;
    }
    if (evaluatorResult.isSuccessful()) {
        jclass optionalClazz = env->FindClass("java/util/Optional");
        auto containerClass = env->FindClass("java/lang/Double");
        auto valueOfMethodId = env->GetStaticMethodID(containerClass, "valueOf", "(D)Ljava/lang/Double;");
        auto javaValue = env->CallStaticObjectMethod(containerClass, valueOfMethodId, (jdouble)evaluatorResult.result->asNumber());
        return env->CallStaticObjectMethod(optionalClazz,
                                           env->GetStaticMethodID(optionalClazz, "of",
                                                                  "(Ljava/lang/Object;)Ljava/util/Optional;"),
                                           javaValue);
    }

    return storeExceptionOnContextAndReturnsIt(env, contextObject, context, evaluatorResult);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_toString(JNIEnv* env, jobject thiz, jobject context)
{
    THROW_NPE_RETURN_NULL(context, "Context");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    ValueRef* thisValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz);

    auto evaluatorResult = Evaluator::execute(contextRef->get(), [](ExecutionStateRef* state, ValueRef* thisValueRef) -> ValueRef* {
        return thisValueRef->toString(state);
    }, thisValueRef);

    return createOptionalValueFromEvaluatorJavaScriptValueResult(env, context, contextRef->get(),
                                                                 evaluatorResult);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_toNumber(JNIEnv* env, jobject thiz, jobject context)
{
    THROW_NPE_RETURN_NULL(context, "Context");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    ValueRef* thisValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz);

    auto evaluatorResult = Evaluator::execute(contextRef->get(), [](ExecutionStateRef* state, ValueRef* thisValueRef) -> ValueRef* {
        return ValueRef::create(thisValueRef->toNumber(state));
    }, thisValueRef);

    return createOptionalValueFromEvaluatorDoubleResult(env, context, contextRef->get(),
                                                        evaluatorResult);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_toInteger(JNIEnv* env, jobject thiz, jobject context)
{
    THROW_NPE_RETURN_NULL(context, "Context");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    ValueRef* thisValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz);

    auto evaluatorResult = Evaluator::execute(contextRef->get(), [](ExecutionStateRef* state, ValueRef* thisValueRef) -> ValueRef* {
        return ValueRef::create(thisValueRef->toInteger(state));
    }, thisValueRef);

    return createOptionalValueFromEvaluatorDoubleResult(env, context, contextRef->get(),
                                                        evaluatorResult);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_toInt32(JNIEnv* env, jobject thiz, jobject context)
{
    THROW_NPE_RETURN_NULL(context, "Context");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    ValueRef* thisValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz);

    auto evaluatorResult = Evaluator::execute(contextRef->get(), [](ExecutionStateRef* state, ValueRef* thisValueRef) -> ValueRef* {
        return ValueRef::create(thisValueRef->toInt32(state));
    }, thisValueRef);

    return createOptionalValueFromEvaluatorIntegerResult(env, context, contextRef->get(),
                                                         evaluatorResult);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_toBoolean(JNIEnv* env, jobject thiz, jobject context)
{
    THROW_NPE_RETURN_NULL(context, "Context");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    ValueRef* thisValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz);

    auto evaluatorResult = Evaluator::execute(contextRef->get(), [](ExecutionStateRef* state, ValueRef* thisValueRef) -> ValueRef* {
        return ValueRef::create(thisValueRef->toBoolean(state));
    }, thisValueRef);

    return createOptionalValueFromEvaluatorBooleanResult(env, context, contextRef->get(),
                                                         evaluatorResult);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_toObject(JNIEnv* env, jobject thiz, jobject context)
{
    THROW_NPE_RETURN_NULL(context, "Context");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    ValueRef* thisValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz);

    auto evaluatorResult = Evaluator::execute(contextRef->get(), [](ExecutionStateRef* state, ValueRef* thisValueRef) -> ValueRef* {
        return thisValueRef->toObject(state);
    }, thisValueRef);

    return createOptionalValueFromEvaluatorJavaScriptValueResult(env, context, contextRef->get(),
                                                                 evaluatorResult);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_abstractEqualsTo(JNIEnv* env, jobject thiz,
                                                               jobject context, jobject other)
{
    THROW_NPE_RETURN_NULL(context, "Context");
    THROW_NPE_RETURN_NULL(other, "JavaScriptValue");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    ValueRef* thisValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz);
    ValueRef* otherValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(other), other);

    auto evaluatorResult = Evaluator::execute(contextRef->get(), [](ExecutionStateRef* state, ValueRef* thisValueRef, ValueRef* otherValueRef) -> ValueRef* {
        return ValueRef::create(thisValueRef->abstractEqualsTo(state, otherValueRef));
    }, thisValueRef, otherValueRef);

    return createOptionalValueFromEvaluatorBooleanResult(env, context, contextRef->get(),
                                                         evaluatorResult);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_equalsTo(JNIEnv* env, jobject thiz, jobject context,
                                                       jobject other)
{
    THROW_NPE_RETURN_NULL(context, "Context");
    THROW_NPE_RETURN_NULL(other, "JavaScriptValue");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    ValueRef* thisValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz);
    ValueRef* otherValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(other), other);

    auto evaluatorResult = Evaluator::execute(contextRef->get(), [](ExecutionStateRef* state, ValueRef* thisValueRef, ValueRef* otherValueRef) -> ValueRef* {
        return ValueRef::create(thisValueRef->equalsTo(state, otherValueRef));
    }, thisValueRef, otherValueRef);

    return createOptionalValueFromEvaluatorBooleanResult(env, context, contextRef->get(),
                                                         evaluatorResult);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_instanceOf(JNIEnv* env, jobject thiz, jobject context,
                                                         jobject other)
{
    THROW_NPE_RETURN_NULL(context, "Context");
    THROW_NPE_RETURN_NULL(other, "JavaScriptValue");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    ValueRef* thisValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz);
    ValueRef* otherValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(other), other);

    auto evaluatorResult = Evaluator::execute(contextRef->get(), [](ExecutionStateRef* state, ValueRef* thisValueRef, ValueRef* otherValueRef) -> ValueRef* {
        return ValueRef::create(thisValueRef->instanceOf(state, otherValueRef));
    }, thisValueRef, otherValueRef);

    return createOptionalValueFromEvaluatorBooleanResult(env, context, contextRef->get(),
                                                         evaluatorResult);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_call(JNIEnv* env, jobject thiz, jobject context,
                                                   jobject receiver, jobjectArray argv)
{
    THROW_NPE_RETURN_NULL(context, "Context");
    THROW_NPE_RETURN_NULL(receiver, "JavaScriptValue");
    THROW_NPE_RETURN_NULL(argv, "JavaScriptValue[]");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    ValueRef* thisValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz);
    ValueRef* receiverValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(receiver), receiver);

    auto argvLength = env->GetArrayLength(argv);
    ValueRef** argVector = reinterpret_cast<ValueRef**>(Memory::gcMalloc(argvLength * sizeof(ValueRef*)));

    for (jsize i = 0; i < argvLength; i++) {
        jobject e = env->GetObjectArrayElement(argv, i);
        argVector[i] = unwrapValueRefFromValue(env, env->GetObjectClass(e), e);
        env->DeleteLocalRef(e);
    }

    auto evaluatorResult = Evaluator::execute(contextRef->get(),
                                              [](ExecutionStateRef* state, ValueRef* thisValueRef, ValueRef* receiverValueRef, ValueRef** argVector, int argvLength) -> ValueRef* {
                                                  return thisValueRef->call(state, receiverValueRef, argvLength, argVector);
                                              }, thisValueRef, receiverValueRef, argVector, argvLength);

    Memory::gcFree(argVector);

    return createOptionalValueFromEvaluatorJavaScriptValueResult(env, context, contextRef->get(),
                                                                 evaluatorResult);
}


extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptValue_construct(JNIEnv* env, jobject thiz, jobject context,
                                                        jobjectArray argv)
{
    THROW_NPE_RETURN_NULL(context, "Context");
    THROW_NPE_RETURN_NULL(argv, "JavaScriptValue[]");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    ValueRef* thisValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz);

    auto argvLength = env->GetArrayLength(argv);
    ValueRef** argVector = reinterpret_cast<ValueRef**>(Memory::gcMalloc(argvLength * sizeof(ValueRef*)));

    for (jsize i = 0; i < argvLength; i++) {
        jobject e = env->GetObjectArrayElement(argv, i);
        argVector[i] = unwrapValueRefFromValue(env, env->GetObjectClass(e), e);
        env->DeleteLocalRef(e);
    }

    auto evaluatorResult = Evaluator::execute(contextRef->get(),
                                              [](ExecutionStateRef* state, ValueRef* thisValueRef, ValueRef** argVector, int argvLength) -> ValueRef* {
                                                  return thisValueRef->construct(state, argvLength, argVector);
                                              }, thisValueRef, argVector, argvLength);

    Memory::gcFree(argVector);

    return createOptionalValueFromEvaluatorJavaScriptValueResult(env, context, contextRef->get(),
                                                                 evaluatorResult);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptSymbol_description(JNIEnv* env, jobject thiz)
{
    auto desc = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->asSymbol()->description();
    return nativeOptionalValueIntoJavaOptionalValue(env, desc);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptSymbol_symbolDescriptiveString(JNIEnv* env, jobject thiz)
{
    auto desc = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->asSymbol()->symbolDescriptiveString();
    return createJavaObjectFromValue(env, desc);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptBigInt_create__I(JNIEnv* env, jclass clazz, jint num)
{
    return createJavaValueObject(env, clazz, BigIntRef::create(static_cast<int64_t>(num)));
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptBigInt_create__J(JNIEnv* env, jclass clazz, jlong num)
{
    return createJavaValueObject(env, clazz, BigIntRef::create(static_cast<int64_t>(num)));
}


extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptBigInt_create__Ljava_lang_String_2I(JNIEnv* env,
                                                                            jclass clazz,
                                                                            jstring numString,
                                                                            jint radix)
{
    return createJavaValueObject(env, clazz,
                                 BigIntRef::create(createJSStringFromJava(env, numString), radix));
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptBigInt_create__Lcom_samsung_lwe_escargot_JavaScriptString_2I(
        JNIEnv* env, jclass clazz, jobject numString, jint radix)
{
    if (numString) {
        return createJavaValueObject(env, clazz,
                                     BigIntRef::create(unwrapValueRefFromValue(env, env->GetObjectClass(
                                             numString), numString)->asString(), radix));
    } else {
        return createJavaValueObject(env, clazz, BigIntRef::create(static_cast<int64_t>(0)));
    }
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptBigInt_toString(JNIEnv* env, jobject thiz, jint radix)
{
    return createJavaObjectFromValue(env, unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->asBigInt()->toString(radix));
}

extern "C"
JNIEXPORT jdouble JNICALL
Java_com_samsung_lwe_escargot_JavaScriptBigInt_toNumber(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->asBigInt()->toNumber();
}

extern "C"
JNIEXPORT jlong JNICALL
Java_com_samsung_lwe_escargot_JavaScriptBigInt_toInt64(JNIEnv* env, jobject thiz)
{
    return unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->asBigInt()->toInt64();
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptSymbol_fromGlobalSymbolRegistry(JNIEnv* env, jclass clazz,
                                                                        jobject vm,
                                                                        jobject stringKey)
{
    THROW_NPE_RETURN_NULL(vm, "VMInstance");
    THROW_NPE_RETURN_NULL(stringKey, "JavaScriptString");

    auto ptr = getPersistentPointerFromJava<VMInstanceRef>(env, env->GetObjectClass(vm), vm);
    auto key = unwrapValueRefFromValue(env, env->GetObjectClass(stringKey), stringKey);

    auto symbol = SymbolRef::fromGlobalSymbolRegistry(ptr->get(), key->asString());
    return createJavaObjectFromValue(env, symbol);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptObject_get(JNIEnv* env, jobject thiz, jobject context,
                                                   jobject propertyName)
{
    THROW_NPE_RETURN_NULL(context, "Context");
    THROW_NPE_RETURN_NULL(propertyName, "JavaScriptValue");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    ObjectRef* thisValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->asObject();
    ValueRef* propertyNameValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(propertyName), propertyName);

    auto evaluatorResult = Evaluator::execute(contextRef->get(), [](ExecutionStateRef* state, ObjectRef* thisValueRef, ValueRef* propertyNameValueRef) -> ValueRef* {
        return thisValueRef->get(state, propertyNameValueRef);
    }, thisValueRef, propertyNameValueRef);

    return createOptionalValueFromEvaluatorJavaScriptValueResult(env, context, contextRef->get(),
                                                                 evaluatorResult);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptObject_set(JNIEnv* env, jobject thiz, jobject context,
                                                   jobject propertyName, jobject value)
{
    THROW_NPE_RETURN_NULL(context, "Context");
    THROW_NPE_RETURN_NULL(propertyName, "JavaScriptValue");
    THROW_NPE_RETURN_NULL(value, "JavaScriptValue");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    ObjectRef* thisValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->asObject();
    ValueRef* propertyNameValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(propertyName), propertyName);
    ValueRef* valueRef = unwrapValueRefFromValue(env, env->GetObjectClass(value), value);

    auto evaluatorResult = Evaluator::execute(contextRef->get(), [](ExecutionStateRef* state, ObjectRef* thisValueRef, ValueRef* propertyNameValueRef, ValueRef* valueRef) -> ValueRef* {
        return ValueRef::create(thisValueRef->set(state, propertyNameValueRef, valueRef));
    }, thisValueRef, propertyNameValueRef, valueRef);

    return createOptionalValueFromEvaluatorBooleanResult(env, context, contextRef->get(),
                                                         evaluatorResult);
}
extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptObject_defineDataProperty(JNIEnv* env, jobject thiz,
                                                                  jobject context,
                                                                  jobject propertyName,
                                                                  jobject value,
                                                                  jboolean isWritable,
                                                                  jboolean isEnumerable,
                                                                  jboolean isConfigurable)
{
    THROW_NPE_RETURN_NULL(context, "Context");
    THROW_NPE_RETURN_NULL(propertyName, "JavaScriptValue");
    THROW_NPE_RETURN_NULL(value, "JavaScriptValue");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    ObjectRef* thisValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->asObject();
    ValueRef* propertyNameValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(propertyName), propertyName);
    ValueRef* valueRef = unwrapValueRefFromValue(env, env->GetObjectClass(value), value);

    auto evaluatorResult = Evaluator::execute(contextRef->get(), [](ExecutionStateRef* state, ObjectRef* thisValueRef, ValueRef* propertyNameValueRef, ValueRef* valueRef,
                                                                    jboolean isWritable,
                                                                    jboolean isEnumerable,
                                                                    jboolean isConfigurable) -> ValueRef* {
        return ValueRef::create(thisValueRef->defineDataProperty(state, propertyNameValueRef, valueRef, isWritable, isEnumerable, isConfigurable));
    }, thisValueRef, propertyNameValueRef, valueRef, isWritable, isEnumerable, isConfigurable);

    return createOptionalValueFromEvaluatorBooleanResult(env, context, contextRef->get(),
                                                         evaluatorResult);
}
extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptObject_getOwnProperty(JNIEnv* env, jobject thiz,
                                                              jobject context,
                                                              jobject propertyName)
{
    THROW_NPE_RETURN_NULL(context, "Context");
    THROW_NPE_RETURN_NULL(propertyName, "JavaScriptValue");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    ObjectRef* thisValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->asObject();
    ValueRef* propertyNameValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(propertyName), propertyName);

    auto evaluatorResult = Evaluator::execute(contextRef->get(), [](ExecutionStateRef* state, ObjectRef* thisValueRef, ValueRef* propertyNameValueRef) -> ValueRef* {
        return thisValueRef->getOwnProperty(state, propertyNameValueRef);
    }, thisValueRef, propertyNameValueRef);

    return createOptionalValueFromEvaluatorJavaScriptValueResult(env, context, contextRef->get(),
                                                                 evaluatorResult);
}
extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptObject_create(JNIEnv* env, jclass clazz, jobject context)
{
    THROW_NPE_RETURN_NULL(context, "Context");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    auto evaluatorResult = Evaluator::execute(contextRef->get(), [](ExecutionStateRef* state) -> ValueRef* {
        return ObjectRef::create(state);
    });

    assert(evaluatorResult.isSuccessful());
    return createJavaObjectFromValue(env, evaluatorResult.result->asObject());
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptArrayObject_create(JNIEnv* env, jclass clazz,
                                                           jobject context)
{
    THROW_NPE_RETURN_NULL(context, "Context");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    auto evaluatorResult = Evaluator::execute(contextRef->get(), [](ExecutionStateRef* state) -> ValueRef* {
        return ArrayObjectRef::create(state);
    });

    assert(evaluatorResult.isSuccessful());
    return createJavaObjectFromValue(env, evaluatorResult.result->asArrayObject());
}

extern "C"
JNIEXPORT jlong JNICALL
Java_com_samsung_lwe_escargot_JavaScriptArrayObject_length(JNIEnv* env, jobject thiz, jobject context)
{
    THROW_NPE_RETURN_NULL(context, "Context");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    ArrayObjectRef* thisValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(thiz), thiz)->asArrayObject();

    int64_t length = 0;
    auto evaluatorResult = Evaluator::execute(contextRef->get(), [](ExecutionStateRef* state, ArrayObjectRef* thisValueRef, int64_t* pLength) -> ValueRef* {
        *pLength = static_cast<int64_t>(thisValueRef->length(state));
        return ValueRef::createUndefined();
    }, thisValueRef, &length);

    return length;
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_Context_getGlobalObject(JNIEnv* env, jobject thiz)
{
    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(thiz), thiz);
    return createJavaObjectFromValue(env, contextRef->get()->globalObject());
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptGlobalObject_jsonStringify(JNIEnv* env, jobject thiz,
                                                                   jobject context, jobject input)
{
    THROW_NPE_RETURN_NULL(context, "Context");
    THROW_NPE_RETURN_NULL(input, "JavaScriptValue");

    auto globalObjectRef = getPersistentPointerFromJava<GlobalObjectRef>(env, env->GetObjectClass(context), thiz);
    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    ValueRef* inputValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(input), input);

    auto evaluatorResult = Evaluator::execute(contextRef->get(), [](ExecutionStateRef* state, GlobalObjectRef* globalObject, ValueRef* inputValueRef) -> ValueRef* {
        return globalObject->jsonStringify()->call(state, globalObject->json(), 1, &inputValueRef);
    }, globalObjectRef->get(), inputValueRef);

    return createOptionalValueFromEvaluatorJavaScriptValueResult(env, context, contextRef->get(),
                                                                 evaluatorResult);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptGlobalObject_jsonParse(JNIEnv* env, jobject thiz,
                                                               jobject context, jobject input)
{
    THROW_NPE_RETURN_NULL(context, "Context");
    THROW_NPE_RETURN_NULL(input, "JavaScriptValue");

    auto globalObjectRef = getPersistentPointerFromJava<GlobalObjectRef>(env, env->GetObjectClass(context), thiz);
    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context), context);
    ValueRef* inputValueRef = unwrapValueRefFromValue(env, env->GetObjectClass(input), input);

    auto evaluatorResult = Evaluator::execute(contextRef->get(), [](ExecutionStateRef* state, GlobalObjectRef* globalObject, ValueRef* inputValueRef) -> ValueRef* {
        return globalObject->jsonParse()->call(state, globalObject->json(), 1, &inputValueRef);
    }, globalObjectRef->get(), inputValueRef);

    return createOptionalValueFromEvaluatorJavaScriptValueResult(env, context, contextRef->get(),
                                                                 evaluatorResult);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_samsung_lwe_escargot_JavaScriptJavaCallbackFunctionObject_create(JNIEnv* env, jclass clazz,
                                                                          jobject context,
                                                                          jstring functionName,
                                                                          jint argumentCount,
                                                                          jboolean isConstructor,
                                                                          jobject callback)
{
    THROW_NPE_RETURN_NULL(context, "Context");
    THROW_NPE_RETURN_NULL(callback, "Callback");

    auto contextRef = getPersistentPointerFromJava<ContextRef>(env, env->GetObjectClass(context),
                                                               context);

    FunctionObjectRef::NativeFunctionInfo info(
        AtomicStringRef::create(contextRef->get(), createJSStringFromJava(env, functionName)),
        [](ExecutionStateRef* state, ValueRef* thisValue, size_t argc, ValueRef** argv, bool isConstructorCall) -> ValueRef* {
            auto env = fetchJNIEnvFromCallback();
            if (!env) {
                LOGE("failed to fetch env from function callback");
                return ValueRef::createUndefined();
            }

            if (env->ExceptionCheck()) {
                throwJavaRuntimeException(state);
                return ValueRef::createUndefined();
            }

            env->PushLocalFrame(32);
            jobject callback = reinterpret_cast<jobject>(state->resolveCallee()->extraData());
            auto callbackMethodId = env->GetMethodID(env->GetObjectClass(callback), "callback",
                                                     "(Lcom/samsung/lwe/escargot/JavaScriptValue;[Lcom/samsung/lwe/escargot/JavaScriptValue;)Ljava/util/Optional;");

            jobjectArray javaArgv = env->NewObjectArray(argc, env->FindClass("com/samsung/lwe/escargot/JavaScriptValue"), nullptr);

            for (size_t i = 0; i < argc; i ++) {
                auto ref = createJavaObjectFromValue(env.get(), argv[i]);
                env->SetObjectArrayElement(javaArgv, i, ref);
                env->DeleteLocalRef(ref);
            }
            jobject returnValue = env->CallObjectMethod(
                    callback,
                    callbackMethodId,
                    createJavaObjectFromValue(env.get(), thisValue),
                    javaArgv
                    );

            if (env->ExceptionCheck()) {
                env->PopLocalFrame(NULL);
                throwJavaRuntimeException(state);
                return ValueRef::createUndefined();
            }

            ValueRef* nativeReturnValue = ValueRef::createUndefined();
            if (returnValue) {
                auto classOptional = env->GetObjectClass(returnValue);
                auto methodIsPresent = env->GetMethodID(classOptional, "isPresent", "()Z");
                if (env->CallBooleanMethod(returnValue, methodIsPresent)) {
                    auto methodGet = env->GetMethodID(classOptional, "get", "()Ljava/lang/Object;");
                    jobject callbackReturnValue = env->CallObjectMethod(returnValue, methodGet);
                    nativeReturnValue = unwrapValueRefFromValue(env.get(), env->GetObjectClass(callbackReturnValue), callbackReturnValue);
                }
            }
            env->PopLocalFrame(NULL);
            return nativeReturnValue;
        },
        argumentCount,
        isConstructor);

    auto evaluatorResult = Evaluator::execute(contextRef->get(),
                                                    [](ExecutionStateRef* state, FunctionObjectRef::NativeFunctionInfo info) -> ValueRef* {
        return FunctionObjectRef::create(state, info);
    }, info);

    assert(evaluatorResult.isSuccessful());

    callback = env->NewGlobalRef(callback);
    FunctionObjectRef* fn = evaluatorResult.result->asFunctionObject();
    fn->setExtraData(callback);
    Memory::gcRegisterFinalizer(fn, [](void* self) {
        jobject jo = static_cast<jobject>(reinterpret_cast<FunctionObjectRef*>(self)->extraData());
        auto env = fetchJNIEnvFromCallback();
        if (env) {
            env->DeleteGlobalRef(jo);
        }
    });

    return createJavaValueObject(env, "com/samsung/lwe/escargot/JavaScriptJavaCallbackFunctionObject", fn);
}
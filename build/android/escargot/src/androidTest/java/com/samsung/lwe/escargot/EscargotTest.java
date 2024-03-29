package com.samsung.lwe.escargot;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.ByteArrayInputStream;
import java.util.Optional;

@RunWith(AndroidJUnit4.class)
public class EscargotTest {
    public void printNegativeTC(String desc) {
        System.out.println("[NEGATIVE TC] " + desc);
    }

    public void printPositiveTC(String desc) {
        System.out.println("[POSITIVE TC] " + desc);
    }


    @Test
    public void initTest() {
        Globals.initializeGlobals();
        // null test
        assertThrows(NullPointerException.class, () -> {
            printNegativeTC("VMInstance create with null 1");
            VMInstance.create(Optional.empty(), null);
        });
        assertThrows(NullPointerException.class, () -> {
            printNegativeTC("VMInstance create with null 2");
            VMInstance.create(null, Optional.empty());
        });
        assertThrows(NullPointerException.class, () -> {
            printNegativeTC("Context create with null 2");
            Context.create(null);
        });

        VMInstance vmInstance = VMInstance.create(Optional.of("en-US"), Optional.of("Asia/Seoul"));
        Context context = Context.create(vmInstance);

        // dump build config
        Evaluator.evalScript(context, "'" + BuildConfig.BUILD_TYPE + "'", null, true);
        Evaluator.evalScript(context, "'" + BuildConfig.LIBRARY_PACKAGE_NAME + "'", null, true);
        Evaluator.evalScript(context, "'" + BuildConfig.DEBUG + "'", null, true);

        // test writable of /tmp/
        Optional<String> s = Escargot.copyStreamAsTempFile(new ByteArrayInputStream("asdf".getBytes()), "test", ".txt", false);
        assertTrue(s.isPresent());

        printPositiveTC("test build config and copy file in jar");

        for (int i = 0; i < 30000; i++) {
            // alloc many trash objects for testing memory management
            JavaScriptValue.create("asdf");
        }

        context = null;
        vmInstance = null;
        Globals.finalizeGlobals();

        printPositiveTC("VMInstance, Context create");
    }

    @Test
    public void initMultipleTimesTest() {
        // pass if there is no crash
        Globals.initializeGlobals();
        Globals.initializeGlobals();
        Globals.initializeGlobals();
        Globals.finalizeGlobals();
        Globals.finalizeGlobals();
        Globals.finalizeGlobals();
        Globals.finalizeGlobals();
        Globals.finalizeGlobals();

        Globals.initializeGlobals();
        Globals.finalizeGlobals();

        Globals.initializeGlobals();
        Globals.finalizeGlobals();

        printNegativeTC("Globals inbalance call test");
    }

    @Test
    public void muitipleVMTest() {
        Globals.initializeGlobals();
        // pass if there is no crash
        // user can create multiple VM
        VMInstance.create(Optional.empty(), Optional.empty());
        VMInstance.create(Optional.of("en-US"), Optional.empty());
        VMInstance.create(Optional.empty(), Optional.of("Asia/Seoul"));
        Globals.finalizeGlobals();

        printPositiveTC("muitipleVMTest");
    }

    @Test
    public void muitipleContextTest() {
        Globals.initializeGlobals();
        // pass if there is no crash
        // user can create multiple context
        VMInstance vm = VMInstance.create(Optional.empty(), Optional.empty());
        Context.create(vm);
        Context.create(vm);
        Context.create(vm);
        Globals.finalizeGlobals();

        printPositiveTC("muitipleContextTest");
    }

    @Test
    public void simpleScriptRunTest() {
        Globals.initializeGlobals();

        VMInstance vmInstance = VMInstance.create(Optional.of("en-US"), Optional.of("Asia/Seoul"));
        Context context = Context.create(vmInstance);

        assertThrows(NullPointerException.class, () -> {
            Evaluator.evalScript(null, "", null, true);
            printNegativeTC("Evaluator.evalScript with null");
        });

        // pass if not assert doesn't throws
        Evaluator.evalScript(context, null, null, true);
        printNegativeTC("Evaluator.evalScript with null 2");

        JavaScriptValue vv = JavaScriptValue.create(System.getProperty("java.version"));
        context.getGlobalObject().set(context, JavaScriptValue.create("ddd"), vv);
        Evaluator.evalScript(context, "'java.version' + ddd", null, true);
        printPositiveTC("Evaluator.evalScript test 1");

        // test script parsing error
        assertFalse(Evaluator.evalScript(context, "@@", "invalid", true).isPresent());
        // test runtime error
        assertFalse(Evaluator.evalScript(context, "throw new Error()", "invalid", true).isPresent());
        printPositiveTC("Evaluator.evalScript test 2");

        assertTrue(Evaluator.evalScript(context, "a = 1", "from_java.js", true).get().toString(context).get().toJavaString().equals("1"));
        assertTrue(Evaluator.evalScript(context, "a", "from_java2.js", true).get().toString(context).get().toJavaString().equals("1"));

        context = null;
        vmInstance = null;
        Globals.finalizeGlobals();

        printPositiveTC("Evaluator.evalScript test 3");
    }

    @Test
    public void attachICUTest() {
        Globals.initializeGlobals();

        VMInstance vmInstance = VMInstance.create(Optional.of("en-US"), Optional.of("Asia/Seoul"));
        Context context = Context.create(vmInstance);

        assertTrue(Evaluator.evalScript(context, "a = new Date('2000/1/1')", "from_java3.js", true).get().toString(context).get().toJavaString().contains("2000"));
        assertTrue(Evaluator.evalScript(context,
                "const koDtf = new Intl.DateTimeFormat(\"ko\", { dateStyle: \"long\" }); koDtf.format(a)", "from_java4.js", true).get().toString(context).get().toJavaString().contains("2000"));

        context = null;
        vmInstance = null;
        Globals.finalizeGlobals();

        printPositiveTC("attachICUTest");
    }

    @Test
    public void simpleOneByOneThreadingTest() {
        Thread t1 = new Thread(() -> {
            assertFalse(Globals.isInitialized());
            Globals.initializeGlobals();

            VMInstance vmInstance = VMInstance.create(Optional.of("en-US"), Optional.of("Asia/Seoul"));
            Context context = Context.create(vmInstance);

            assertEquals(JavaScriptValue.create(123).toString(context).get().toJavaString(), "123");
            assertEquals(JavaScriptValue.create(123).toBoolean(context).get(), true);

            JavaScriptValue vv = JavaScriptValue.create(System.getProperty("java.version"));
            context.getGlobalObject().set(context, JavaScriptValue.create("ddd"), vv);
            Evaluator.evalScript(context, "'java.version' + ddd", "invalid", true);

            for (int i = 0; i < 30000; i++) {
                // alloc many trash objects for testing memory management
                JavaScriptValue.create("asdf");
            }

            context = null;
            vmInstance = null;
            System.gc();
            Memory.gc();
            Globals.finalizeGlobals();
        });
        t1.start();
        try {
            t1.join();
        } catch (InterruptedException e) {
            e.printStackTrace();
        }

        Thread t2 = new Thread(() -> {
            assertFalse(Globals.isInitialized());
            Globals.initializeGlobals();

            VMInstance vmInstance = VMInstance.create(Optional.of("en-US"), Optional.of("Asia/Seoul"));
            Context context = Context.create(vmInstance);

            String testString = "[1, 2, 3]";
            JavaScriptValue result = context.getGlobalObject().jsonParse(context, JavaScriptValue.create(testString)).get();
            assertTrue(result.isArrayObject());

            JavaScriptValue vv = JavaScriptValue.create(System.getProperty("java.version"));
            context.getGlobalObject().set(context, JavaScriptValue.create("ddd"), vv);
            Evaluator.evalScript(context, "'java.version' + ddd", "invalid", true);

            context = null;
            vmInstance = null;
            System.gc();
            Memory.gc();
            Globals.finalizeGlobals();
        });

        t2.start();
        try {
            t2.join();
        } catch (InterruptedException e) {
            e.printStackTrace();
        }

        printPositiveTC("simpleOneByOneThreadingTest");
    }

    @Test
    public void simpleMultiThreadingTest() {
        Globals.initializeGlobals();

        Thread t1 = new Thread(() -> {
            assertFalse(Globals.isInitialized());
            Globals.initializeThread();

            VMInstance vmInstance = VMInstance.create(Optional.of("en-US"), Optional.of("Asia/Seoul"));
            Context context = Context.create(vmInstance);

            assertEquals(JavaScriptValue.create(123).toString(context).get().toJavaString(), "123");
            assertEquals(JavaScriptValue.create(123).toBoolean(context).get(), true);

            JavaScriptValue vv = JavaScriptValue.create(System.getProperty("java.version"));
            context.getGlobalObject().set(context, JavaScriptValue.create("ddd"), vv);
            Evaluator.evalScript(context, "'java.version' + ddd", "invalid", true);

            for (int i = 0; i < 999999; i++) {
                // alloc many trash objects for testing memory management
                JavaScriptValue.create("asdf");
            }

            context = null;
            vmInstance = null;
            System.gc();
            Memory.gc();
            Globals.finalizeThread();
        });

        Thread t2 = new Thread(() -> {
            assertFalse(Globals.isInitialized());
            Globals.initializeThread();

            VMInstance vmInstance = VMInstance.create(Optional.of("en-US"), Optional.of("Asia/Seoul"));
            Context context = Context.create(vmInstance);

            String testString = "[1, 2, 3]";
            JavaScriptValue result = context.getGlobalObject().jsonParse(context, JavaScriptValue.create(testString)).get();
            assertTrue(result.isArrayObject());

            JavaScriptValue vv = JavaScriptValue.create(System.getProperty("java.version"));
            context.getGlobalObject().set(context, JavaScriptValue.create("ddd"), vv);
            Evaluator.evalScript(context, "'java.version' + ddd", "invalid", true);

            for (int i = 0; i < 999999; i++) {
                // alloc many trash objects for testing memory management
                JavaScriptValue.create("asdf");
            }

            context = null;
            vmInstance = null;
            System.gc();
            Memory.gc();
            Globals.finalizeThread();
        });

        t1.start();
        t2.start();

        try {
            t1.join();
            t2.join();
        } catch (InterruptedException e) {
            e.printStackTrace();
        }

        assertFalse(t1.isAlive());
        assertFalse(t2.isAlive());

        System.gc();
        Memory.gc();
        Globals.finalizeGlobals();

        printPositiveTC("simpleMultiThreadingTest");
    }

    private Context initEngineAndCreateContext() {
        Globals.initializeGlobals();
        VMInstance vmInstance = VMInstance.create(Optional.of("en-US"), Optional.of("Asia/Seoul"));
        return Context.create(vmInstance);
    }

    private void finalizeEngine() {
        System.gc();
        System.gc();
        System.gc();
        Memory.gc();
        Memory.gc();
        Globals.finalizeGlobals();
    }

    @Test
    public void bridgeTest() {
        Context context = initEngineAndCreateContext();

        class EmptyBridge extends Bridge.Adapter {
            @Override
            public Optional<JavaScriptValue> callback(Optional<JavaScriptValue> data) {
                return Optional.empty();
            }
        }

        assertThrows(NullPointerException.class, () -> {
            Bridge.register(null, "a", "b", new EmptyBridge());
        });
        printNegativeTC("register bridge with null 1");
        assertFalse(Bridge.register(context, null, "b", new EmptyBridge()));
        printNegativeTC("register bridge with null 2");
        assertFalse(Bridge.register(context, "", "b", new EmptyBridge()));
        printNegativeTC("register bridge with null 3");
        assertFalse(Bridge.register(context, "a", null, new EmptyBridge()));
        printNegativeTC("register bridge with null 4");
        assertFalse(Bridge.register(context, "a", "", new EmptyBridge()));
        printNegativeTC("register bridge with null 5");

        class TestBridge extends Bridge.Adapter {
            public boolean called = false;

            @Override
            public Optional<JavaScriptValue> callback(Optional<JavaScriptValue> data) {
                assertFalse(called);
                assertTrue(data.get().isString());
                assertTrue(data.get().asScriptString().toJavaString().equals("dddd"));
                called = true;
                return Optional.of(JavaScriptValue.create(data.get().asScriptString().toJavaString() + "ASdfasdfasdf"));
            }
        };

        TestBridge testBridge = new TestBridge();
        Bridge.register(context, "Native", "addString", testBridge);
        printPositiveTC("register bridge 1");
        Bridge.register(context, "Native", "returnString", new Bridge.Adapter() {
            @Override
            public Optional<JavaScriptValue> callback(Optional<JavaScriptValue> data) {
                assertFalse(data.isPresent());
                return Optional.of(JavaScriptValue.create("string from java"));
            }
        });
        printPositiveTC("register bridge 2");
        Bridge.register(context, "Native", "returnNothing", new Bridge.Adapter() {
            @Override
            public Optional<JavaScriptValue> callback(Optional<JavaScriptValue> data) {
                return Optional.empty();
            }
        });

        assertTrue(Evaluator.evalScript(context, "Native.addString('dddd')", "from_java5.js", true).get().asScriptString().toJavaString().equals("ddddASdfasdfasdf"));
        assertTrue(testBridge.called);
        printPositiveTC("register bridge 3");

        assertTrue(Evaluator.evalScript(context, "Native.returnString()", "from_java6.js", true).get().asScriptString().toJavaString().equals("string from java"));
        printPositiveTC("register bridge 4");
        assertTrue(Evaluator.evalScript(context, "Native.returnNothing() === undefined", "from_java7.js", true).get().toString(context).get().toJavaString().equals("true"));
        printNegativeTC("register bridge return null");

        Bridge.register(context, "Native", "returnNull", new Bridge.Adapter() {
            @Override
            public Optional<JavaScriptValue> callback(Optional<JavaScriptValue> data) {
                return null;
            }
        });

        assertTrue(Evaluator.evalScript(context, "Native.returnNull() === undefined", "from_java8.js", true).get().toString(context).get().toJavaString().equals("true"));
        printNegativeTC("register bridge return null 2");

        Bridge.register(context, "Native", "runtimeException", new Bridge.Adapter() {
            @Override
            public Optional<JavaScriptValue> callback(Optional<JavaScriptValue> data) {
                throw new RuntimeException("test");
            }
        });
        {
            final Context finalContext = context;
            assertThrows(RuntimeException.class, () -> {
                Evaluator.evalScript(finalContext, "Native.runtimeException()", "from_java9.js", true);
            });
        }
        printNegativeTC("register bridge throws exception");

        context = null;
        finalizeEngine();
    }

    @Test
    public void nonHeapValueTest() {
        Context context = initEngineAndCreateContext();

        JavaScriptValue v = JavaScriptValue.createUndefined();
        assertTrue(v.isUndefined());
        assertFalse(v.isNull());
        assertFalse(v.isNumber());
        printPositiveTC("undefined value test");

        v = JavaScriptValue.createNull();
        assertTrue(v.isNull());
        assertFalse(v.isUndefined());
        assertFalse(v.isNumber());
        printPositiveTC("null value test");

        v = JavaScriptValue.create(123);
        assertTrue(v.isInt32());
        assertEquals(v.asInt32(), 123);
        assertTrue(v.isNumber());
        assertEquals(v.asNumber(), 123.0, 0);
        printPositiveTC("number value test");

        v = JavaScriptValue.create(true);
        assertTrue(v.isTrue());
        assertTrue(v.isBoolean());
        assertTrue(v.asBoolean());
        assertFalse(v.isInt32());
        assertFalse(v.isUndefined());

        v = JavaScriptValue.create(false);
        assertTrue(v.isFalse());
        assertTrue(v.isBoolean());
        assertFalse(v.asBoolean());
        assertFalse(v.isInt32());
        assertFalse(v.isUndefined());
        printPositiveTC("bool value test");

        context = null;
        finalizeEngine();
    }

    @Test
    public void heapValueTest() {
        Context context = initEngineAndCreateContext();

        JavaScriptValue v = JavaScriptValue.create(3.14);
        assertTrue(v.isNumber());
        assertEquals(v.asNumber(), 3.14, 0);
        assertFalse(v.isInt32());
        printPositiveTC("float value test");

        v = JavaScriptValue.create((String) null);
        assertTrue(v.isString());
        assertTrue(v.asScriptString().toJavaString().equals(""));
        printNegativeTC("string with null");

        v = JavaScriptValue.create("hello");
        assertTrue(v.isString());
        assertTrue(v.asScriptString().toJavaString().equals("hello"));
        assertFalse(v.isNumber());
        assertFalse(v.isUndefinedOrNull());
        printPositiveTC("string value test");

        context = null;
        finalizeEngine();
    }

    @Test
    public void valueToStringTest() {
        Context context = initEngineAndCreateContext();

        Optional<JavaScriptString> result = JavaScriptValue.create(123).toString(context);
        assertTrue(result.isPresent());
        assertEquals(result.get().toJavaString(), "123");

        assertTrue(JavaScriptValue.create(Integer.MAX_VALUE).isInt32());
        assertTrue(JavaScriptValue.create(Integer.MAX_VALUE).isNumber());
        result = JavaScriptValue.create(Integer.MAX_VALUE).toString(context);
        assertTrue(result.isPresent());
        assertEquals(result.get().toJavaString(), Integer.MAX_VALUE + "");

        printPositiveTC("value toString test");

        context = null;
        finalizeEngine();
    }

    @Test
    public void valueOperationTest() {
        Context context = initEngineAndCreateContext();

        assertThrows(NullPointerException.class, () -> {
            JavaScriptValue.create(123).toString(null);
        });
        printNegativeTC("value method null test 1");
        assertThrows(NullPointerException.class, () -> {
            JavaScriptValue.create(123).toBoolean(null);
        });
        printNegativeTC("value method null test 2");
        assertThrows(NullPointerException.class, () -> {
            JavaScriptValue.create(123).toInteger(null);
        });
        printNegativeTC("value method null test 3");
        assertThrows(NullPointerException.class, () -> {
            JavaScriptValue.create(123).toInt32(null);
        });
        printNegativeTC("value method null test 4");
        assertThrows(NullPointerException.class, () -> {
            JavaScriptValue.create(123).toNumber(null);
        });
        printNegativeTC("value method null test 5");
        assertThrows(NullPointerException.class, () -> {
            JavaScriptValue.create(123).toObject(null);
        });
        printNegativeTC("value method null test 6");

        assertEquals(JavaScriptValue.create(123).toString(context).get().toJavaString(), "123");
        printPositiveTC("value test method test 1");
        assertEquals(JavaScriptValue.create(123).toBoolean(context).get(), true);
        assertEquals(JavaScriptValue.create(0).toBoolean(context).get(), false);
        printPositiveTC("value test method test 2");
        assertEquals(JavaScriptValue.create(123.123).toInteger(context).get(), Double.valueOf(123.0));
        assertEquals(JavaScriptValue.create(123.456).toInt32(context).get(), Integer.valueOf(123));
        printPositiveTC("value test method test 3");
        assertEquals(JavaScriptValue.create("123").toNumber(context).get(), Double.valueOf(123));
        assertTrue(JavaScriptValue.create("123").toObject(context).get().isObject());
        printPositiveTC("value test method test 4");
        assertFalse(context.lastThrownException().isPresent());
        assertFalse(JavaScriptValue.createUndefined().toObject(context).isPresent());
        assertTrue(context.lastThrownException().isPresent());

        context = null;
        finalizeEngine();
    }

    @Test
    public void symbolValueTest() {
        Globals.initializeGlobals();
        VMInstance vmInstance = VMInstance.create(Optional.of("en-US"), Optional.of("Asia/Seoul"));
        Context context = Context.create(vmInstance);

        JavaScriptSymbol symbol = JavaScriptValue.create(Optional.empty());
        assertFalse(symbol.description().isPresent());
        assertTrue(symbol.symbolDescriptiveString().toJavaString().equals("Symbol()"));
        printNegativeTC("symbol null test 1");

        symbol = JavaScriptValue.create((Optional<JavaScriptString>) null);
        assertFalse(symbol.description().isPresent());
        assertTrue(symbol.symbolDescriptiveString().toJavaString().equals("Symbol()"));
        printNegativeTC("symbol null test 2");

        symbol = JavaScriptValue.create(Optional.of(JavaScriptString.create("foobar")));
        assertTrue(symbol.description().isPresent());
        assertTrue(symbol.description().get().toJavaString().equals("foobar"));
        assertFalse(symbol.equalsTo(context, JavaScriptValue.create(Optional.of(JavaScriptString.create("foobar")))).get().booleanValue());
        printNegativeTC("symbol null test 4");

        Optional<JavaScriptValue> exception = context.lastThrownException();
        assertFalse(exception.isPresent());
        Optional<JavaScriptString> shouldBeEmpty = symbol.toString(context);
        assertFalse(shouldBeEmpty.isPresent());
        exception = context.lastThrownException();
        assertTrue(exception.isPresent());
        assertEquals(exception.get().toString(context).get().toJavaString(), "TypeError: Cannot convert a Symbol value to a string");
        printNegativeTC("symbol null test 5");

        assertThrows(NullPointerException.class, () -> {
            JavaScriptSymbol.fromGlobalSymbolRegistry(null, JavaScriptString.create("foo"));
        });
        printNegativeTC("symbol null test 6");
        assertThrows(NullPointerException.class, () -> {
            JavaScriptSymbol.fromGlobalSymbolRegistry(vmInstance, null);
        });
        printNegativeTC("symbol null test 7");

        JavaScriptSymbol symbol1 = JavaScriptSymbol.fromGlobalSymbolRegistry(vmInstance, JavaScriptString.create("foo"));
        JavaScriptSymbol symbol2 = JavaScriptSymbol.fromGlobalSymbolRegistry(vmInstance, JavaScriptString.create("foo"));
        assertTrue(symbol1.equalsTo(context, symbol2).get().booleanValue());
        printPositiveTC("symbol test 1");

        context = null;
        finalizeEngine();
    }

    @Test
    public void objectCreateReadWriteTest() {
        Context context = initEngineAndCreateContext();

        assertThrows(NullPointerException.class, () -> {
            JavaScriptObject.create((Context) null);
        });
        printNegativeTC("object null test 1");

        JavaScriptObject obj = JavaScriptObject.create(context);

        {
            final Context finalContext = context;
            assertThrows(NullPointerException.class, () -> {
                obj.get(null, JavaScriptValue.create("asdf"));
            });
            printNegativeTC("object null test 2");
            assertThrows(NullPointerException.class, () -> {
                obj.get(finalContext, null);
            });
            printNegativeTC("object null test 3");
            assertThrows(NullPointerException.class, () -> {
                obj.set(null, JavaScriptValue.create("asdf"), JavaScriptValue.create("asdf"));
            });
            printNegativeTC("object null test 4");
            assertThrows(NullPointerException.class, () -> {
                obj.set(finalContext, null, JavaScriptValue.create("asdf"));
            });
            printNegativeTC("object null test 5");
            assertThrows(NullPointerException.class, () -> {
                obj.set(finalContext, JavaScriptValue.create("asdf"), null);
            });
            printNegativeTC("object null test 6");
        }

        assertTrue(obj.set(context, JavaScriptValue.create("asdf"), JavaScriptValue.create(123)).get().booleanValue());
        printPositiveTC("object test 1");
        assertTrue(obj.get(context, JavaScriptValue.create("asdf")).get().toNumber(context).get().doubleValue() == 123);
        printPositiveTC("object test 2");

        {
            final Context finalContext = context;
            assertThrows(NullPointerException.class, () -> {
                obj.defineDataProperty(null, JavaScriptValue.create("qwer"), JavaScriptValue.create(123), false, false, false);
            });
            printNegativeTC("object null test 6");
            assertThrows(NullPointerException.class, () -> {
                obj.defineDataProperty(finalContext, null, JavaScriptValue.create(123), false, false, false);
            });
            printNegativeTC("object null test 7");
            assertThrows(NullPointerException.class, () -> {
                obj.defineDataProperty(finalContext, null, null, false, false, false);
            });
            printNegativeTC("object null test 8");
        }

        assertTrue(obj.defineDataProperty(context, JavaScriptValue.create("qwer"), JavaScriptValue.create(123), false, false, false).get().booleanValue());
        printPositiveTC("object test 3");
        assertFalse(obj.defineDataProperty(context, JavaScriptValue.create("qwer"), JavaScriptValue.create(456), false, true, true).get().booleanValue());
        printPositiveTC("object test 4");

        {
            final Context finalContext = context;
            assertThrows(NullPointerException.class, () -> {
                obj.getOwnProperty(null, JavaScriptValue.create("qwer"));
            });
            printNegativeTC("object null test 9");
            assertThrows(NullPointerException.class, () -> {
                obj.getOwnProperty(finalContext, null);
            });
            printNegativeTC("object null test 10");
        }

        assertTrue(obj.getOwnProperty(context, JavaScriptValue.create("qwer")).get().toNumber(context).get().doubleValue() == 123);
        printPositiveTC("object test 5");

        context = null;
        finalizeEngine();
    }

    @Test
    public void arrayCreateReadWriteTest() {
        Context context = initEngineAndCreateContext();

        assertThrows(NullPointerException.class, () -> {
            JavaScriptArrayObject.create((Context) null);
        });
        printNegativeTC("array null test 1");

        JavaScriptArrayObject arr = JavaScriptArrayObject.create(context);
        assertTrue(arr.set(context, JavaScriptValue.create(3), JavaScriptValue.create(123)).get().booleanValue());
        assertTrue(arr.get(context, JavaScriptValue.create(3)).get().toNumber(context).get().doubleValue() == 123);
        assertTrue(arr.get(context, JavaScriptValue.create("length")).get().toInt32(context).get().intValue() == 4);
        assertTrue(arr.length(context) == 4);
        printPositiveTC("array test 1");

        assertThrows(NullPointerException.class, () -> {
            arr.length(null);
        });
        printNegativeTC("array null test 2");

        context = null;
        finalizeEngine();
    }

    @Test
    public void jsonParseStringifyTest() {
        Context context = initEngineAndCreateContext();

        String testString = "[1, 2, 3]";

        {
            final Context finalContext = context;
            String finalTestString = testString;
            assertThrows(NullPointerException.class, () -> {
                finalContext.getGlobalObject().jsonParse(null, JavaScriptValue.create(finalTestString));
            });
            printNegativeTC("json null test 1");
            assertThrows(NullPointerException.class, () -> {
                finalContext.getGlobalObject().jsonParse(finalContext, null);
            });
            printNegativeTC("json null test 2");
        }

        JavaScriptValue result = context.getGlobalObject().jsonParse(context, JavaScriptValue.create(testString)).get();
        assertTrue(result.isArrayObject());
        assertEquals(result.asScriptArrayObject().get(context, JavaScriptValue.create(0)).get().toNumber(context).get().intValue(), 1);
        assertEquals(result.asScriptArrayObject().get(context, JavaScriptValue.create(1)).get().toNumber(context).get().intValue(), 2);
        assertEquals(result.asScriptArrayObject().get(context, JavaScriptValue.create(2)).get().toNumber(context).get().intValue(), 3);
        assertEquals(result.asScriptArrayObject().length(context), 3);
        printPositiveTC("json test 1");

        testString = "{\"a\": \"asdf\"}";
        result = context.getGlobalObject().jsonParse(context, JavaScriptValue.create(testString)).get();
        assertTrue(result.isObject());
        assertEquals(result.asScriptObject().get(context, JavaScriptValue.create("a")).get().asScriptString().toJavaString(), "asdf");
        result.asScriptObject().set(context, JavaScriptValue.create(123), JavaScriptValue.create(456));
        printPositiveTC("json test 2");

        {
            final Context finalContext = context;
            assertThrows(NullPointerException.class, () -> {
                finalContext.getGlobalObject().jsonStringify(null, JavaScriptValue.create(100));
            });
            printNegativeTC("json null test 3");
            assertThrows(NullPointerException.class, () -> {
                finalContext.getGlobalObject().jsonStringify(finalContext, null);
            });
            printNegativeTC("json null test 4");
        }

        assertEquals(context.getGlobalObject().jsonStringify(context, result).get().toJavaString(), "{\"123\":456,\"a\":\"asdf\"}");
        printPositiveTC("json test 3");

        context = null;
        finalizeEngine();
    }

    @Test
    public void testCallableAndCall() {
        Context context = initEngineAndCreateContext();

        JavaScriptValue value = JavaScriptString.create(1123);
        assertFalse(value.isCallable());
        value = JavaScriptString.create("1123");
        assertFalse(value.isCallable());
        printPositiveTC("callable test 1");
        value = JavaScriptString.create(Optional.of(JavaScriptString.create("asdf")));
        assertFalse(value.isCallable());
        value = JavaScriptObject.create(context);
        assertFalse(value.isCallable());
        value = JavaScriptArrayObject.create(context);
        assertFalse(value.isCallable());
        printPositiveTC("callable test 2");

        {
            final Context finalContext = context;
            assertThrows(NullPointerException.class, () -> {
                JavaScriptValue.create("asdf").call(null, JavaScriptValue.createUndefined(), new JavaScriptValue[]{});
            });
            printNegativeTC("callable null test 1");
            assertThrows(NullPointerException.class, () -> {
                JavaScriptValue.create("asdf").call(finalContext, null, new JavaScriptValue[]{});
            });
            printNegativeTC("callable null test 2");
            assertThrows(NullPointerException.class, () -> {
                JavaScriptValue.create("asdf").call(finalContext, JavaScriptValue.createUndefined(), null);
            });
            printNegativeTC("callable null test 3");
        }

        assertFalse(context.lastThrownException().isPresent());
        value.call(context, JavaScriptValue.createUndefined(), new JavaScriptValue[]{});
        assertTrue(context.lastThrownException().isPresent());
        printPositiveTC("callable test 3");

        value = context.getGlobalObject().asScriptObject().get(context, JavaScriptValue.create("Array")).get();
        assertTrue(value.isCallable());

        value = value.call(context, JavaScriptString.createUndefined(), new JavaScriptValue[]{
                JavaScriptValue.create(1), JavaScriptValue.create(2), JavaScriptValue.create(3)
        }).get();

        assertTrue(value.isArrayObject());
        assertEquals(value.asScriptArrayObject().length(context), 3);
        assertEquals(value.asScriptArrayObject().get(context, JavaScriptValue.create(0)).get().asInt32(), 1);
        assertEquals(value.asScriptArrayObject().get(context, JavaScriptValue.create(1)).get().asInt32(), 2);
        assertEquals(value.asScriptArrayObject().get(context, JavaScriptValue.create(2)).get().asInt32(), 3);
        printPositiveTC("callable test 4");

        // receiver test
        JavaScriptGlobalObject global = context.getGlobalObject();
        JavaScriptValue globalFunction = global.get(context, JavaScriptString.create("Function")).get();
        JavaScriptValue newFunction = globalFunction.call(context, JavaScriptValue.createUndefined(),
                new JavaScriptValue[]{JavaScriptString.create("return this")}).get();
        assertTrue(newFunction.isFunctionObject());
        JavaScriptValue ret = newFunction.call(context, JavaScriptValue.createUndefined(), new JavaScriptValue[]{}).get();
        assertTrue(ret.equalsTo(context, global).get().booleanValue());

        ret = newFunction.call(context, JavaScriptValue.create("asdf"), new JavaScriptValue[]{}).get();
        assertTrue(ret.isObject());
        assertEquals(ret.toString(context).get().toJavaString(), "asdf");
        printPositiveTC("callable test 5");

        context = null;
        finalizeEngine();
    }

    @Test
    public void testBigInt() {
        Context context = initEngineAndCreateContext();

        JavaScriptBigInt bigInt = JavaScriptBigInt.create(123123);
        assertEquals(bigInt.toString(10).toJavaString(), "123123");
        assertEquals(bigInt.toString(3).toJavaString(), "20020220010");
        assertEquals(bigInt.toInt64(), 123123);
        assertTrue(bigInt.isBigInt());
        assertFalse(JavaScriptValue.createNull().isBigInt());
        printPositiveTC("BigInt test 1");

        bigInt = JavaScriptBigInt.create("123123", 10);
        assertEquals(bigInt.toInt64(), 123123);

        bigInt = JavaScriptBigInt.create(JavaScriptString.create("123123"), 10);
        assertEquals(bigInt.toInt64(), 123123);

        assertTrue(bigInt.equalsTo(context, JavaScriptBigInt.create("20020220010", 3)).get().booleanValue());
        printPositiveTC("BigInt test 2");

        bigInt = JavaScriptBigInt.create(Long.MAX_VALUE + "0", 10);
        assertEquals(bigInt.toString(10).toJavaString(), Long.MAX_VALUE + "0");
        assertEquals(bigInt.toInt64(), -10);
        printNegativeTC("BigInt value test 1");

        bigInt = JavaScriptBigInt.create(Long.MAX_VALUE);
        assertEquals(bigInt.toInt64(), Long.MAX_VALUE);
        printNegativeTC("BigInt value test 2");

        bigInt = JavaScriptBigInt.create(Long.MIN_VALUE);
        assertEquals(bigInt.toInt64(), Long.MIN_VALUE);
        printNegativeTC("BigInt value test 3");

        assertEquals(JavaScriptBigInt.create((String) null, 10).toInt64(), 0);
        assertEquals(JavaScriptBigInt.create((JavaScriptString) null, 10).toInt64(), 0);
        printNegativeTC("BigInt value test 4");

        context = null;
        finalizeEngine();
    }

    @Test
    public void testConstruct() {
        Context context = initEngineAndCreateContext();

        {
            final Context finalContext = context;
            assertThrows(NullPointerException.class, () -> {
                JavaScriptValue.create("asdf").construct(null, new JavaScriptValue[]{});
            });
            printNegativeTC("construct value test 1");
            assertThrows(NullPointerException.class, () -> {
                JavaScriptValue.create("asdf").construct(finalContext, null);
            });
            printNegativeTC("construct value test 2");
        }

        JavaScriptGlobalObject global = context.getGlobalObject();
        JavaScriptValue globalFunction = global.get(context, JavaScriptString.create("Function")).get();
        JavaScriptValue newFunction = globalFunction.construct(context, new JavaScriptValue[]{
                JavaScriptString.create("a"),
                JavaScriptString.create("return a")
        }).get();

        assertTrue(newFunction.isFunctionObject());
        printPositiveTC("construct test 1");

        JavaScriptValue returnValue = newFunction.call(context, JavaScriptValue.createUndefined(), new JavaScriptValue[]{
                JavaScriptValue.create("test")
        }).get();

        assertEquals(returnValue.asScriptString().toJavaString(), "test");
        printPositiveTC("construct test 2");

        context = null;
        finalizeEngine();
    }

    @Test
    public void testJavaCallbackFunction() {
        Context context = initEngineAndCreateContext();

        {
            final Context finalContext = context;
            assertThrows(NullPointerException.class, () -> {
                JavaScriptJavaCallbackFunctionObject.create(null,
                        null,
                        3,
                        false,
                        new JavaScriptJavaCallbackFunctionObject.Callback() {
                            @Override
                            public Optional<JavaScriptValue> callback(JavaScriptValue receiverValue, JavaScriptValue[] arguments) {
                                return Optional.empty();
                            }
                        });
            });
            printNegativeTC("callback null test 1");
            assertThrows(NullPointerException.class, () -> {
                JavaScriptJavaCallbackFunctionObject.create(finalContext,
                        null,
                        3,
                        false,
                        null);
            });
            printNegativeTC("callback null test 2");
        }

        JavaScriptJavaCallbackFunctionObject callbackFunctionObject =
                JavaScriptJavaCallbackFunctionObject.create(context,
                        "fnname",
                        3,
                        false,
                        new JavaScriptJavaCallbackFunctionObject.Callback() {
                            @Override
                            public Optional<JavaScriptValue> callback(JavaScriptValue receiverValue, JavaScriptValue[] arguments) {
                                return Optional.of(JavaScriptValue.create(arguments.length));
                            }
                        });

        context.getGlobalObject().set(context, JavaScriptString.create("asdf"), callbackFunctionObject);

        Optional<JavaScriptValue> ret = Evaluator.evalScript(context, "asdf.name", "test.js", false);
        assertEquals(ret.get().asScriptString().toJavaString(), "fnname");

        ret = Evaluator.evalScript(context, "asdf.length", "test.js", false);
        assertEquals(ret.get().asInt32(), 3);
        printPositiveTC("construct test 1");

        assertFalse(context.exceptionWasThrown());
        Evaluator.evalScript(context, "new asdf()", "test.js", false);
        assertFalse(context.lastThrownException().isPresent());

        ret = Evaluator.evalScript(context, "asdf(1, 2, 3, 4)", "test.js", false);
        assertEquals(ret.get().asInt32(), 4);

        ret = Evaluator.evalScript(context, "asdf(1, 2)", "test.js", false);
        assertEquals(ret.get().asInt32(), 2);
        printPositiveTC("construct test 2");

        callbackFunctionObject =
                JavaScriptJavaCallbackFunctionObject.create(context,
                        "fnname",
                        3,
                        false,
                        new JavaScriptJavaCallbackFunctionObject.Callback() {
                            @Override
                            public Optional<JavaScriptValue> callback(JavaScriptValue receiverValue, JavaScriptValue[] arguments) {
                                int sum = 0;
                                for (int i = 0; i < arguments.length; i++) {
                                    sum += arguments[i].asInt32();
                                }
                                return Optional.of(JavaScriptValue.create(sum));
                            }
                        });
        context.getGlobalObject().set(context, JavaScriptString.create("asdf"), callbackFunctionObject);

        ret = Evaluator.evalScript(context, "asdf(1, 2, 3, 4)", "test.js", false);
        assertEquals(ret.get().asInt32(), 10);

        ret = Evaluator.evalScript(context, "asdf(1, 2)", "test.js", false);
        assertEquals(ret.get().asInt32(), 3);
        printPositiveTC("construct test 3");

        callbackFunctionObject =
                JavaScriptJavaCallbackFunctionObject.create(context,
                        "fnname",
                        0,
                        false,
                        new JavaScriptJavaCallbackFunctionObject.Callback() {
                            @Override
                            public Optional<JavaScriptValue> callback(JavaScriptValue receiverValue, JavaScriptValue[] arguments) {
                                return null;
                            }
                        });
        context.getGlobalObject().set(context, JavaScriptString.create("asdf"), callbackFunctionObject);

        ret = Evaluator.evalScript(context, "asdf()", "test.js", false);
        assertTrue(ret.get().isUndefined());
        printNegativeTC("callback null test 3");

        callbackFunctionObject =
                JavaScriptJavaCallbackFunctionObject.create(context,
                        "fnname",
                        0,
                        false,
                        new JavaScriptJavaCallbackFunctionObject.Callback() {
                            @Override
                            public Optional<JavaScriptValue> callback(JavaScriptValue receiverValue, JavaScriptValue[] arguments) {
                                throw new RuntimeException("test");
                            }
                        });
        context.getGlobalObject().set(context, JavaScriptString.create("asdf"), callbackFunctionObject);
        {
            final Context finalContext1 = context;
            assertThrows(RuntimeException.class, () -> {
                finalContext1.getGlobalObject().get(finalContext1, JavaScriptString.create("asdf")).get().call(finalContext1, JavaScriptValue.createUndefined(), new JavaScriptValue[]{});
            });
        }
        printNegativeTC("callback RuntimeException test");

        context = null;
        finalizeEngine();
    }

    @Test
    public void nanInfIssueTest() {
        Context context = initEngineAndCreateContext();

        Optional<JavaScriptValue> ret = Evaluator.evalScript(context, "(this.NaN + '') == 'NaN'", "test.js", false);
        assertTrue(ret.isPresent());
        assertTrue(ret.get().isTrue());
        printNegativeTC("nanInfIssueTest test 1");

        ret = Evaluator.evalScript(context, "(this.Infinity + '') == 'Infinity'", "test.js", false);
        assertTrue(ret.isPresent());
        assertTrue(ret.get().isTrue());
        printNegativeTC("nanInfIssueTest test 2");

        ret = Evaluator.evalScript(context, "(parseFloat(undefined) + '') == 'NaN'", "test.js", false);
        assertTrue(ret.isPresent());
        assertTrue(ret.get().isTrue());
        printNegativeTC("nanInfIssueTest test 3");

        ret = Evaluator.evalScript(context, "(undefined + undefined) ? 'a' : 'b' == 'b'", "test.js", false);
        assertTrue(ret.isPresent());
        assertTrue(ret.get().isTrue());
        printNegativeTC("nanInfIssueTest test 4");

        context = null;
        finalizeEngine();
    }

    @Test
    public void promiseTest()
    {
        Globals.initializeGlobals();
        VMInstance vmInstance = VMInstance.create(Optional.of("en-US"), Optional.of("Asia/Seoul"));
        Context context = Context.create(vmInstance);

        Evaluator.evalScript(context, "var myResolve\n" +
                "var myPromise = new Promise((resolve, reject) => {\n" +
                "  myResolve = resolve;\n" +
                "});\n" +
                "myPromise.then( () => { globalThis.thenCalled = true; } )", "test.js", true);

        assertTrue(context.getGlobalObject().get(context, JavaScriptValue.create("thenCalled")).get().isUndefined());
        Evaluator.evalScript(context, "myResolve()", "test.js", true);
        assertTrue(context.getGlobalObject().get(context, JavaScriptValue.create("thenCalled")).get().asBoolean());
        assertFalse(vmInstance.hasPendingJob());
        printPositiveTC("promiseTest 1");

        context = Context.create(vmInstance);
        Evaluator.evalScript(context, "var myResolve\n" +
                "var myPromise = new Promise((resolve, reject) => {\n" +
                "  myResolve = resolve;\n" +
                "});\n" +
                "myPromise.then( () => { globalThis.thenCalled = true; } )", "test.js", true);
        context.getGlobalObject().get(context, JavaScriptValue.create("myResolve")).get().call(context, JavaScriptValue.createUndefined(), new JavaScriptValue[]{});
        assertTrue(vmInstance.hasPendingJob());
        vmInstance.executeEveryPendingJobIfExists();
        assertTrue(context.getGlobalObject().get(context, JavaScriptValue.create("thenCalled")).get().asBoolean());
        assertFalse(vmInstance.hasPendingJob());
        printPositiveTC("promiseTest 2");

        context = null;
        vmInstance = null;
        finalizeEngine();
    }

}
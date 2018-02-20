/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import java.lang.annotation.Annotation;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.reflect.Constructor;
import java.lang.reflect.Parameter;

public class Main {
    // A simple parameter annotation
    @Retention(RetentionPolicy.RUNTIME)
    public @interface AnnotationA {}

    // A parameter annotation with additional state
    @Retention(RetentionPolicy.RUNTIME)
    public @interface AnnotationB {
        String value() default "default-value";
    }

    // An inner class whose constructors with have an implicit
    // argument for the enclosing instance.
    public class Inner {
        private final int number;
        private final String text;
        boolean flag;

        Inner(@AnnotationA int number, String text) {
            this.number = number;
            this.text = text;
            this.flag = false;
        }

        Inner(@AnnotationA int number, String text, @AnnotationB("x") boolean flag) {
            this.number = number;
            this.text = text;
            this.flag = flag;
        }
    }

    // An inner class whose constructors with have no implicit
    // arguments for the enclosing instance.
    public static class StaticInner {
        private final int number;
        private final String text;
        boolean flag;

        StaticInner(@AnnotationA int number, String text) {
            this.number = number;
            this.text = text;
            this.flag = false;
        }

        StaticInner(@AnnotationB("foo") int number, String text, @AnnotationA boolean flag) {
            this.number = number;
            this.text = text;
            this.flag = flag;
        }
    }

    public enum ImportantNumber {
        ONE(1.0),
        TWO(2.0),
        MANY(3.0, true);

        private double doubleValue;
        private boolean isLarge;

        ImportantNumber(@AnnotationA double doubleValue) {
            this.doubleValue = doubleValue;
            this.isLarge = false;
        }

        ImportantNumber(@AnnotationB("x") double doubleValue, @AnnotationB("y") boolean isLarge) {
            this.doubleValue = doubleValue;
            this.isLarge = isLarge;
        }
    }

    public enum BinaryNumber {
        ZERO,
        ONE;
    }

    private static void DumpConstructorParameterAnnotations(Class<?> cls) {
        System.out.println(cls.getName());
        for (Constructor c : cls.getDeclaredConstructors()) {
            System.out.println(" " + c);
            Annotation[][] annotations = c.getParameterAnnotations();
            Parameter[] parameters = c.getParameters();
            for (int i = 0; i < annotations.length; ++i) {
                // Exercise java.lang.reflect.Executable.getParameterAnnotationsNative()
                // which retrieves all annotations for the parameters.
                System.out.print("  Parameter [" + i + "]:");
                for (Annotation annotation : parameters[i].getAnnotations()) {
                    // String.replace() to accomodate different representation across VMs.
                    System.out.println("    Indexed : " + annotation.toString().replace("\"", ""));
                }
                for (Annotation annotation : annotations[i]) {
                    // String.replace() to accomodate different representation across VMs.
                    System.out.println("    Array : " + annotation.toString().replace("\"", ""));
                }

                // Exercise Parameter.getAnnotationNative() with
                // retrieves a single parameter annotation according to type.
                Annotation byType = parameters[i].getDeclaredAnnotation(AnnotationA.class);
                String hasA = (byType != null ? "Yes" : "No");
                System.out.println("     AnnotationA.IsPresent : " + hasA);
            }
        }
    }

    public static void main(String[] args) {
        // A local class which by definition has no implicit parameters.
        class LocalClass {
            private final int integerValue;
            LocalClass(@AnnotationA int integerValue) {
                this.integerValue = integerValue;
            }
        }

        DumpConstructorParameterAnnotations(Main.class);
        DumpConstructorParameterAnnotations(LocalClass.class);
        DumpConstructorParameterAnnotations(Inner.class);
        DumpConstructorParameterAnnotations(StaticInner.class);
        DumpConstructorParameterAnnotations(ImportantNumber.class);
        DumpConstructorParameterAnnotations(BinaryNumber.class);
    }
}

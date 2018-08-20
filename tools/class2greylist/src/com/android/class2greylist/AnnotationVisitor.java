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

package com.android.class2greylist;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Joiner;
import com.google.common.base.Preconditions;

import org.apache.bcel.Const;
import org.apache.bcel.classfile.AnnotationElementValue;
import org.apache.bcel.classfile.AnnotationEntry;
import org.apache.bcel.classfile.ArrayElementValue;
import org.apache.bcel.classfile.DescendingVisitor;
import org.apache.bcel.classfile.ElementValue;
import org.apache.bcel.classfile.ElementValuePair;
import org.apache.bcel.classfile.EmptyVisitor;
import org.apache.bcel.classfile.Field;
import org.apache.bcel.classfile.FieldOrMethod;
import org.apache.bcel.classfile.JavaClass;
import org.apache.bcel.classfile.Method;
import org.apache.bcel.classfile.SimpleElementValue;

import java.util.Locale;
import java.util.Set;
import java.util.function.Predicate;

/**
 * Visits a JavaClass instance and pulls out all members annotated with a
 * specific annotation. The signatures of such members are passed to {@link
 * GreylistConsumer#greylistEntry(String, Integer)}. Any errors result in a
 * call to {@link Status#error(String, Object...)}.
 *
 * If the annotation has a property "expectedSignature" the generated signature
 * will be verified against the one specified there. If it differs, an error
 * will be generated.
 */
public class AnnotationVisitor extends EmptyVisitor {

    // properties of greylist annotations:
    private static final String EXPECTED_SIGNATURE = "expectedSignature";
    private static final String MAX_TARGET_SDK = "maxTargetSdk";

    // properties of whitelist annotations:
    private static final String RETURN_TYPE = "returnType";

    private final JavaClass mClass;
    private final String mGreylistAnnotationType;
    private final Set<String> mWhitelistAnnotations;
    private final Predicate<GreylistMember> mGreylistFilter;
    private final Set<Integer> mValidMaxTargetSdkValues;
    private final GreylistConsumer mConsumer;
    private final Status mStatus;
    private final DescendingVisitor mDescendingVisitor;

    /**
     * Represents a member of a class file (a field or method).
     */
    @VisibleForTesting
    public static class GreylistMember {

        /**
         * Signature of this member.
         */
        public final String signature;
        /**
         * Indicates if this is a synthetic bridge method.
         */
        public final boolean bridge;
        /**
         * Max target SDK of property this member, if it is set, else null.
         *
         * Note: even though the annotation itself specified a default value,
         * that default value is not encoded into instances of the annotation
         * in class files. So when no value is specified in source, it will
         * result in null appearing in here.
         */
        public final Integer maxTargetSdk;

        public GreylistMember(String signature, boolean bridge, Integer maxTargetSdk) {
            this.signature = signature;
            this.bridge = bridge;
            this.maxTargetSdk = maxTargetSdk;
        }
    }

    public AnnotationVisitor(JavaClass clazz, String greylistAnnotation,
            Set<String> whitelistAnnotations, Set<String> publicApis,
            Set<Integer> validMaxTargetSdkValues, GreylistConsumer consumer,
            Status status) {
        this(clazz,
                greylistAnnotation,
                whitelistAnnotations,
                member -> !(member.bridge && publicApis.contains(member.signature)),
                validMaxTargetSdkValues,
                consumer,
                status);
    }

    @VisibleForTesting
    public AnnotationVisitor(JavaClass clazz, String greylistAnnotation,
            Set<String> whitelistAnnotations, Predicate<GreylistMember> memberFilter,
            Set<Integer> validMaxTargetSdkValues,
            GreylistConsumer consumer, Status status) {
        mClass = clazz;
        mGreylistAnnotationType = greylistAnnotation;
        mWhitelistAnnotations = whitelistAnnotations;
        mGreylistFilter = memberFilter;
        mValidMaxTargetSdkValues = validMaxTargetSdkValues;
        mConsumer = consumer;
        mStatus = status;
        mDescendingVisitor = new DescendingVisitor(clazz, this);
        Preconditions.checkState(!mWhitelistAnnotations.contains(greylistAnnotation));
    }

    public void visit() {
        mStatus.debug("Visit class %s", mClass.getClassName());
        mDescendingVisitor.visit();
    }

    private static String getClassDescriptor(JavaClass clazz) {
        // JavaClass.getName() returns the Java-style name (with . not /), so we must fetch
        // the original class name from the constant pool.
        return clazz.getConstantPool().getConstantString(
                clazz.getClassNameIndex(), Const.CONSTANT_Class);
    }

    @Override
    public void visitMethod(Method method) {
        visitMember(method, "L%s;->%s%s");
    }

    @Override
    public void visitField(Field field) {
        visitMember(field, "L%s;->%s:%s");
    }

    private void visitMember(FieldOrMethod member, String signatureFormatString) {
        mStatus.debug("Visit member %s : %s", member.getName(), member.getSignature());
        for (AnnotationEntry a : member.getAnnotationEntries()) {
            if (mGreylistAnnotationType.equals(a.getAnnotationType())) {
                mStatus.debug("Member has annotation %s", mGreylistAnnotationType);
                handleGreylistEntry(member, signatureFormatString, a);
            }
            if (mWhitelistAnnotations.contains(a.getAnnotationType())) {
                mStatus.debug("Member annotation %s is for whitelist", a.getAnnotationType());
                handleWhitelistEntry(member, signatureFormatString, a);
            }
        }
    }

    private void handleGreylistEntry(FieldOrMethod member, String signatureFormatString,
            AnnotationEntry a) {
        JavaClass definingClass = (JavaClass) mDescendingVisitor.predecessor();
        // For fields, the same access flag means volatile, so only check for methods.
        boolean bridge = (member instanceof Method)
                && (member.getAccessFlags() & Const.ACC_BRIDGE) != 0;
        if (bridge) {
            mStatus.debug("Member is a bridge", mGreylistAnnotationType);
        }
        String signature = String.format(Locale.US, signatureFormatString,
                getClassDescriptor(definingClass), member.getName(), member.getSignature());
        Integer maxTargetSdk = null;
        for (ElementValuePair property : a.getElementValuePairs()) {
            switch (property.getNameString()) {
                case EXPECTED_SIGNATURE:
                    verifyExpectedSignature(
                            property, signature, definingClass, member, bridge);
                    break;
                case MAX_TARGET_SDK:
                    maxTargetSdk = verifyAndGetMaxTargetSdk(
                            property, definingClass, member);
                    break;
            }
        }
        if (mGreylistFilter.test(new GreylistMember(signature, bridge, maxTargetSdk))) {
            mConsumer.greylistEntry(signature, maxTargetSdk);
        }
    }

    private void handleWhitelistEntry(FieldOrMethod member, String signatureFormatString,
            AnnotationEntry a) {
        JavaClass definingClass = (JavaClass) mDescendingVisitor.predecessor();
        String returnType = null;
        for (ElementValuePair property : a.getElementValuePairs()) {
            switch (property.getNameString()) {
            case RETURN_TYPE:
                returnType = property.getValue().stringifyValue();
                break;
            case "value":
                mStatus.debug("Annotation specifies value");
                boolean handled = false;
                if (property.getValue() instanceof ArrayElementValue) {
                    mStatus.debug("Value is an array");
                    ArrayElementValue array = (ArrayElementValue) property.getValue();
                    for (ElementValue v : array.getElementValuesArray()) {
                        if (v instanceof AnnotationElementValue) {
                            mStatus.debug("Array entry is an annotation; recursing");
                            AnnotationElementValue aev = (AnnotationElementValue) v;
                            handleWhitelistEntry(
                                    member, signatureFormatString, aev.getAnnotationEntry());
                            handled = true;
                        }
                    }
                }
                if (handled) {
                    return;
                }
                break;
            }
        }
        String typeSignature = member.getSignature();
        if (returnType != null) {
            mStatus.debug("Member specifies own return type: %s", returnType);
            if (!(member instanceof Method)) {
                error(definingClass, member, "Cannot specify %s on a field", RETURN_TYPE);
                return;
            }
            int closingBrace = typeSignature.indexOf(')');
            Preconditions.checkState(closingBrace != -1);
            typeSignature = new StringBuilder()
                    .append(typeSignature.substring(0, closingBrace + 1))
                    .append(returnType)
                    .toString();
        }
        // TODO verify that the member is not already a public API
        String signature = String.format(Locale.US, signatureFormatString,
                getClassDescriptor(definingClass), member.getName(), typeSignature);
        mConsumer.whitelistEntry(signature);
    }


    private void verifyExpectedSignature(ElementValuePair property, String signature,
            JavaClass definingClass, FieldOrMethod member, boolean isBridge) {
        String expected = property.getValue().stringifyValue();
        // Don't enforce for bridge methods; they're generated so won't match.
        if (!isBridge && !signature.equals(expected)) {
            error(definingClass, member,
                    "Expected signature does not match generated:\n"
                            + "Expected:  %s\n"
                            + "Generated: %s", expected, signature);
        }
    }

    private Integer verifyAndGetMaxTargetSdk(
            ElementValuePair property, JavaClass definingClass, FieldOrMethod member) {
        if (property.getValue().getElementValueType() != ElementValue.PRIMITIVE_INT) {
            error(definingClass, member, "Expected property %s to be of type int; got %d",
                    property.getNameString(), property.getValue().getElementValueType());
        }
        int value = ((SimpleElementValue) property.getValue()).getValueInt();
        if (!mValidMaxTargetSdkValues.contains(value)) {
            error(definingClass, member,
                    "Invalid value for %s: got %d, expected one of [%s]",
                    property.getNameString(),
                    value,
                    Joiner.on(",").join(mValidMaxTargetSdkValues));
            return null;
        }
        return value;
    }

    private void error(JavaClass clazz, FieldOrMethod member, String message, Object... args) {
        StringBuilder error = new StringBuilder();
        error.append(clazz.getSourceFileName())
                .append(": ")
                .append(clazz.getClassName())
                .append(".")
                .append(member.getName())
                .append(": ")
                .append(String.format(Locale.US, message, args));

        mStatus.error(error.toString());
    }

}

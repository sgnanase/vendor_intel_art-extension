/*
 * Copyright (C) 2015 The Android Open Source Project
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

public class Main {

  /// CHECK-START: int Main.div() licm (before)
  /// CHECK-DAG: Div loop:{{B\d+}}

  /// CHECK-START: int Main.div() licm (after)
  /// CHECK-NOT: Div loop:{{B\d+}}

  /// CHECK-START: int Main.div() licm (after)
  /// CHECK-DAG: Div loop:none

  public static boolean neverThrow = false;

  public static int div() {
    int result = 0;
    for (int i = 0; i < 10; ++i) {
      if (neverThrow)
        throw new Error();
      result += staticField / 42;
    }
    return result;
  }

  /// CHECK-START: int Main.innerDiv() licm (before)
  /// CHECK-DAG: Div loop:{{B\d+}}

  /// CHECK-START: int Main.innerDiv() licm (after)
  /// CHECK-NOT: Div loop:{{B\d+}}

  /// CHECK-START: int Main.innerDiv() licm (after)
  /// CHECK-DAG: Div loop:none

  public static int innerDiv() {
    int result = 0;
    for (int i = 0; i < 10; ++i) {
      for (int j = 0; j < 10; ++j) {
        result += staticField / 42;
      }
    }
    return result;
  }

  /// CHECK-START: int Main.innerMul() licm (before)
  /// CHECK-DAG: Mul loop:B4

  /// CHECK-START: int Main.innerMul() licm (after)
  /// CHECK-DAG: Mul loop:B2

  public static int innerMul() {
    int result = 0;
    for (int i = 0; i < 10; ++i) {
      for (int j = 0; j < 10; ++j) {
        // The operation has been hoisted out of the inner loop.
        // Note that we depend on the compiler's block numbering to
        // check if it has been moved.
        if (neverThrow) {
          throw new Error();
        }
        result += staticField * i;
      }
    }
    return result;
  }

  /// CHECK-START: int Main.divByA(int, int) licm (before)
  /// CHECK-DAG: Div loop:{{B\d+}}

  /// CHECK-START: int Main.divByA(int, int) licm (after)
  /// CHECK-DAG: Div loop:{{B\d+}}

  public static int divByA(int a, int b) {
    int result = 0;
    while (b < 5) {
      // a might be null, so we can't hoist the operation.
      if (neverThrow) {
        throw new Error();
      }
      result += staticField / a;
      b++;
    }
    return result;
  }

  /// CHECK-START: int Main.arrayLength(int[]) licm (before)
  /// CHECK-DAG: [[NullCheck:l\d+]] NullCheck ( loop_header:null )
  /// CHECK-DAG:                    ArrayLength [ [[NullCheck]] ] ( loop_header:null )

  /// CHECK-START: int Main.arrayLength(int[]) licm (after)
  /// CHECK-NOT:                    NullCheck loop:{{B\d+}}
  /// CHECK-NOT:                    ArrayLength loop:{{B\d+}}

  /// CHECK-START: int Main.arrayLength(int[]) licm (after)
  /// CHECK-DAG: <<NullCheck:l\d+>> NullCheck loop:none
  /// CHECK-DAG:                    ArrayLength [<<NullCheck>>] loop:none

  public static int arrayLength(int[] array) {
    int result = 0;
    for (int i = 0; i < array.length; ++i) {
      result += array[i];
    }
    return result;
  }

  /// CHECK-START: int Main.divAndIntrinsic(int[]) licm (before)
  /// CHECK-DAG: Div loop:{{B\d+}}

  /// CHECK-START: int Main.divAndIntrinsic(int[]) licm (after)
  /// CHECK-NOT: Div loop:{{B\d+}}

  /// CHECK-START: int Main.divAndIntrinsic(int[]) licm (after)
  /// CHECK-DAG: Div loop:none

  public static int divAndIntrinsic(int[] array) {
    int result = 0;
    for (int i = 0; i < array.length; i++) {
      // An intrinsic call, unlike a general method call, cannot modify the field value.
      // As a result, the invariant division on the field can be moved out of the loop.
      result += (staticField / 42) + Math.abs(array[i]);
    }
    return result;
  }

  /// CHECK-START: int Main.invariantBoundIntrinsic(int) licm (before)
  /// CHECK-DAG: InvokeStaticOrDirect loop:{{B\d+}}

  /// CHECK-START: int Main.invariantBoundIntrinsic(int) licm (after)
  /// CHECK-NOT: InvokeStaticOrDirect loop:{{B\d+}}

  /// CHECK-START: int Main.invariantBoundIntrinsic(int) licm (after)
  /// CHECK-DAG: InvokeStaticOrDirect loop:none

  public static int invariantBoundIntrinsic(int x) {
    int result = 0;
    // The intrinsic call to abs used as loop bound is invariant.
    // As a result, the call itself can be moved out of the loop header.
    for (int i = 0; i < Math.abs(x); i++) {
      result += i;
    }
    return result;
  }

  /// CHECK-START: int Main.invariantBodyIntrinsic(int, int) licm (before)
  /// CHECK-DAG: InvokeStaticOrDirect loop:{{B\d+}}

  /// CHECK-START: int Main.invariantBodyIntrinsic(int, int) licm (after)
  /// CHECK-NOT: InvokeStaticOrDirect loop:{{B\d+}}

  /// CHECK-START: int Main.invariantBodyIntrinsic(int, int) licm (after)
  /// CHECK-DAG: InvokeStaticOrDirect loop:none

  public static int invariantBodyIntrinsic(int x, int y) {
    int result = 0;
    for (int i = 0; i < 10; i++) {
      // The intrinsic call to max used inside the loop is invariant.
      // As a result, the call itself can be moved out of the loop body.
      result += Math.max(x, y);
    }
    return result;
  }

  public static int staticField = 42;

  public static void assertEquals(int expected, int actual) {
    if (expected != actual) {
      throw new Error("Expected " + expected + ", got " + actual);
    }
  }

  public static void main(String[] args) {
    assertEquals(10, div());
    assertEquals(100, innerDiv());
    assertEquals(18900, innerMul());
    assertEquals(105, divByA(2, 0));
    assertEquals(12, arrayLength(new int[] { 4, 8 }));
    assertEquals(21, divAndIntrinsic(new int[] { 4, -2, 8, -3 }));
    assertEquals(45, invariantBoundIntrinsic(-10));
    assertEquals(30, invariantBodyIntrinsic(2, 3));
  }
}

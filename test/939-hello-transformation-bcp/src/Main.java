/*
 * Copyright (C) 2016 The Android Open Source Project
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

import static art.Redefinition.doCommonClassRedefinition;
import java.util.Base64;
import java.util.OptionalLong;
public class Main {

  /**
   * This is the base64 encoded class/dex.
   *
   * To regenerate these constants:
   *  1) Update src-optional/java/util/OptionalLong.java
   *  2) run convert-to-base64.sh script, specifying
   *     required parameters (path to d8 tool ana path to android.jar;
   *     both can be found in Android sdk)
   *  3) copy and paste base64 text below
   *
   */
  private static final byte[] CLASS_BYTES = Base64.getDecoder().decode(
      "cGFja2FnZSBqYXZhLnV0aWw7CmltcG9ydCBqYXZhLnV0aWwuZnVuY3Rpb24uTG9uZ0NvbnN1bWVy" +
      "OwppbXBvcnQgamF2YS51dGlsLmZ1bmN0aW9uLkxvbmdTdXBwbGllcjsKaW1wb3J0IGphdmEudXRp" +
      "bC5mdW5jdGlvbi5TdXBwbGllcjsKaW1wb3J0IGphdmEudXRpbC5zdHJlYW0uTG9uZ1N0cmVhbTsK" +
      "cHVibGljIGZpbmFsIGNsYXNzIE9wdGlvbmFsTG9uZyB7CiAgLy8gTWFrZSBzdXJlIHdlIGhhdmUg" +
      "YSA8Y2xpbml0PiBmdW5jdGlvbiBzaW5jZSB0aGUgcmVhbCBpbXBsZW1lbnRhdGlvbiBvZiBPcHRp" +
      "b25hbExvbmcgZG9lcy4KICBzdGF0aWMgeyBFTVBUWSA9IG51bGw7IH0KICBwcml2YXRlIHN0YXRp" +
      "YyBmaW5hbCBPcHRpb25hbExvbmcgRU1QVFk7CiAgcHJpdmF0ZSBmaW5hbCBib29sZWFuIGlzUHJl" +
      "c2VudDsKICBwcml2YXRlIGZpbmFsIGxvbmcgdmFsdWU7CiAgcHJpdmF0ZSBPcHRpb25hbExvbmco" +
      "KSB7IGlzUHJlc2VudCA9IGZhbHNlOyB2YWx1ZSA9IDA7IH0KICBwcml2YXRlIE9wdGlvbmFsTG9u" +
      "Zyhsb25nIGwpIHsgdGhpcygpOyB9CiAgcHVibGljIHN0YXRpYyBPcHRpb25hbExvbmcgZW1wdHko" +
      "KSB7IHJldHVybiBudWxsOyB9CiAgcHVibGljIHN0YXRpYyBPcHRpb25hbExvbmcgb2YobG9uZyB2" +
      "YWx1ZSkgeyByZXR1cm4gbnVsbDsgfQogIHB1YmxpYyBsb25nIGdldEFzTG9uZygpIHsgcmV0dXJu" +
      "IDA7IH0KICBwdWJsaWMgYm9vbGVhbiBpc1ByZXNlbnQoKSB7IHJldHVybiBmYWxzZTsgfQogIHB1" +
      "YmxpYyBib29sZWFuIGlzRW1wdHkoKSB7IHJldHVybiBmYWxzZTsgfQogIHB1YmxpYyB2b2lkIGlm" +
      "UHJlc2VudChMb25nQ29uc3VtZXIgYykgeyB9CiAgcHVibGljIHZvaWQgaWZQcmVzZW50T3JFbHNl" +
      "KExvbmdDb25zdW1lciBhY3Rpb24sIFJ1bm5hYmxlIGVtcHR5QWN0aW9uKSB7IH0KICBwdWJsaWMg" +
      "TG9uZ1N0cmVhbSBzdHJlYW0oKSB7IHJldHVybiBudWxsOyB9CiAgcHVibGljIGxvbmcgb3JFbHNl" +
      "KGxvbmcgbCkgeyByZXR1cm4gMDsgfQogIHB1YmxpYyBsb25nIG9yRWxzZUdldChMb25nU3VwcGxp" +
      "ZXIgcykgeyByZXR1cm4gMDsgfQogIHB1YmxpYyBsb25nIG9yRWxzZVRocm93KCkgeyByZXR1cm4g" +
      "MDsgfQogIHB1YmxpYzxYIGV4dGVuZHMgVGhyb3dhYmxlPiBsb25nIG9yRWxzZVRocm93KFN1cHBs" +
      "aWVyPD8gZXh0ZW5kcyBYPiBzKSB0aHJvd3MgWCB7IHJldHVybiAwOyB9CiAgcHVibGljIGJvb2xl" +
      "YW4gZXF1YWxzKE9iamVjdCBvKSB7IHJldHVybiBmYWxzZTsgfQogIHB1YmxpYyBpbnQgaGFzaENv" +
      "ZGUoKSB7IHJldHVybiAwOyB9CiAgcHVibGljIFN0cmluZyB0b1N0cmluZygpIHsgcmV0dXJuICJS" +
      "ZWRlZmluZWQgT3B0aW9uYWxMb25nISI7IH0KfQo=");
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
      "ZGV4CjAzNQA4Wy8s2q0yLrjgivJbIrow+NTwzO02SQskCQAAcAAAAHhWNBIAAAAAAAAAAGAIAAAw" +
      "AAAAcAAAAA8AAAAwAQAADwAAAGwBAAADAAAAIAIAABMAAAA4AgAAAQAAANACAAA0BgAA8AIAAL4E" +
      "AADMBAAA0QQAANsEAADjBAAA5wQAAO4EAADxBAAA9AQAAPgEAAD8BAAA/wQAAAMFAAAiBQAAPgUA" +
      "AFIFAABoBQAAfAUAAJMFAACtBQAA0AUAAPMFAAASBgAAMQYAAFAGAABjBgAAfAYAAH8GAACDBgAA" +
      "hwYAAIwGAACPBgAAkwYAAJoGAACiBgAArQYAALcGAADCBgAA0wYAANwGAADnBgAA6wYAAPMGAAD+" +
      "BgAACwcAABMHAAAdBwAAJAcAAAYAAAAHAAAADAAAAA0AAAAOAAAADwAAABAAAAARAAAAEgAAABMA" +
      "AAAUAAAAFQAAABcAAAAaAAAAHgAAAAYAAAAAAAAAAAAAAAcAAAABAAAAAAAAAAgAAAABAAAAkAQA" +
      "AAkAAAABAAAAmAQAAAkAAAABAAAAoAQAAAoAAAAGAAAAAAAAAAoAAAAIAAAAAAAAAAsAAAAIAAAA" +
      "kAQAAAoAAAAMAAAAAAAAABoAAAANAAAAAAAAABsAAAANAAAAkAQAABwAAAANAAAAqAQAAB0AAAAN" +
      "AAAAsAQAAB4AAAAOAAAAAAAAAB8AAAAOAAAAuAQAAAgACAAFAAAACAAOACcAAAAIAAEALgAAAAQA" +
      "CQADAAAACAAJAAIAAAAIAAkAAwAAAAgACgADAAAACAAGACAAAAAIAA4AIQAAAAgAAQAiAAAACAAA" +
      "ACMAAAAIAAsAJAAAAAgADAAlAAAACAANACYAAAAIAA0AJwAAAAgABwAoAAAACAACACkAAAAIAAMA" +
      "KgAAAAgAAQArAAAACAAEACsAAAAIAAgALAAAAAgABQAtAAAACAAAABEAAAAEAAAAAAAAABgAAABI" +
      "CAAA3QcAAAAAAAACAAIAAAAAAAAAAAACAAAAEgEPAQIAAQAAAAAAAAAAAAIAAAASAA8AAgABAAAA" +
      "AAAAAAAAAgAAABIADwACAAEAAAAAAAAAAAACAAAAEgAPAAIAAQAAAAAAAAAAAAMAAAAaABkAEQAA" +
      "AAEAAAAAAAAAAAAAAAIAAAASABEAAgACAAAAAAAAAAAAAgAAABIAEQACAAEAAAAAAAAAAAACAAAA" +
      "EgARAAMAAQAAAAAAAAAAAAMAAAAWAAAAEAAAAAMAAwAAAAAAAAAAAAMAAAAWAQAAEAEAAAQAAgAA" +
      "AAAAAAAAAAMAAAAWAAAAEAAAAAMAAQAAAAAAAAAAAAMAAAAWAAAAEAAAAAQAAgAAAAAAAAAAAAMA" +
      "AAAWAAAAEAAAAAAAAAAAAAAAAAAAAAEAAAAOAAAAAwABAAEAAACGBAAACwAAAHAQAAACABIAXCAB" +
      "ABYAAABaIAIADgAAAAMAAwABAAAAigQAAAQAAABwEAIAAAAOAAIAAgAAAAAAAAAAAAEAAAAOAAAA" +
      "AwADAAAAAAAAAAAAAQAAAA4ADAAOAA0BAA4AAAEAAAABAAAAAQAAAAoAAAABAAAACwAAAAEAAAAJ" +
      "AAAAAgAAAAkABQABAAAABAAMK1RYOz47KUpeVFg7AAM8WDoACDxjbGluaXQ+AAY8aW5pdD4AAj4o" +
      "AAVFTVBUWQABSQABSgACSkoAAkpMAAFMAAJMSgAdTGRhbHZpay9hbm5vdGF0aW9uL1NpZ25hdHVy" +
      "ZTsAGkxkYWx2aWsvYW5ub3RhdGlvbi9UaHJvd3M7ABJMamF2YS9sYW5nL09iamVjdDsAFExqYXZh" +
      "L2xhbmcvUnVubmFibGU7ABJMamF2YS9sYW5nL1N0cmluZzsAFUxqYXZhL2xhbmcvVGhyb3dhYmxl" +
      "OwAYTGphdmEvdXRpbC9PcHRpb25hbExvbmc7ACFMamF2YS91dGlsL2Z1bmN0aW9uL0xvbmdDb25z" +
      "dW1lcjsAIUxqYXZhL3V0aWwvZnVuY3Rpb24vTG9uZ1N1cHBsaWVyOwAdTGphdmEvdXRpbC9mdW5j" +
      "dGlvbi9TdXBwbGllcjsAHUxqYXZhL3V0aWwvZnVuY3Rpb24vU3VwcGxpZXI8AB1MamF2YS91dGls" +
      "L3N0cmVhbS9Mb25nU3RyZWFtOwART3B0aW9uYWxMb25nLmphdmEAF1JlZGVmaW5lZCBPcHRpb25h" +
      "bExvbmchAAFWAAJWSgACVkwAA1ZMTAABWgACWkwABWVtcHR5AAZlcXVhbHMACWdldEFzTG9uZwAI" +
      "aGFzaENvZGUACWlmUHJlc2VudAAPaWZQcmVzZW50T3JFbHNlAAdpc0VtcHR5AAlpc1ByZXNlbnQA" +
      "Am9mAAZvckVsc2UACW9yRWxzZUdldAALb3JFbHNlVGhyb3cABnN0cmVhbQAIdG9TdHJpbmcABXZh" +
      "bHVlAJ4Bfn5EOHsiYmFja2VuZCI6ImRleCIsImNvbXBpbGF0aW9uLW1vZGUiOiJyZWxlYXNlIiwi" +
      "aGFzLWNoZWNrc3VtcyI6ZmFsc2UsIm1pbi1hcGkiOjEsInNoYS0xIjoiOWM5OGM2ZGRmZDc0ZGVj" +
      "ZThiOTdlOGEyODc4ZDIwOGEwNjJmZGJmNCIsInZlcnNpb24iOiIzLjAuNDEtZGV2In0AAgIBLhwF" +
      "FwEXERcEFxYXAAIDAS4cARgHAQIFDQAaARIBEgGIgASMCAGCgASgCAGCgATICAEJ2AYICewGBQHw" +
      "BQEBlAcBAawGAQHgCAEB9AgBAYQGAQGYBgIBrAcBAcQHAQHcBwEB9AcBAYAHAQHABgAAAAAAAAAC" +
      "AAAAxQcAANUHAAA4CAAAAAAAAAEAAAAAAAAAEAAAADwIAAAQAAAAAAAAAAEAAAAAAAAAAQAAADAA" +
      "AABwAAAAAgAAAA8AAAAwAQAAAwAAAA8AAABsAQAABAAAAAMAAAAgAgAABQAAABMAAAA4AgAABgAA" +
      "AAEAAADQAgAAASAAABIAAADwAgAAAyAAAAIAAACGBAAAARAAAAYAAACQBAAAAiAAADAAAAC+BAAA" +
      "BCAAAAIAAADFBwAAACAAAAEAAADdBwAAAxAAAAIAAAA4CAAABiAAAAEAAABICAAAABAAAAEAAABg" +
      "CAAA");

  public static void main(String[] args) {
    // OptionalLong is a class that is unlikely to be used by the time this test starts and is not
    // likely to be changed in any meaningful way in the future.
    OptionalLong ol = OptionalLong.of(0xDEADBEEF);
    System.out.println("ol.toString() -> '" + ol.toString() + "'");
    System.out.println("Redefining OptionalLong!");
    doCommonClassRedefinition(OptionalLong.class, CLASS_BYTES, DEX_BYTES);
    System.out.println("ol.toString() -> '" + ol.toString() + "'");
  }
}

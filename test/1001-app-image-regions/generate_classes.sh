#!/bin/bash

for i in $(seq 1 $1); do
  echo "  static class Inner${i} { void test(){} }"
done

for i in $(seq 1 $1); do
  echo "LMain\$Inner${i};"
done



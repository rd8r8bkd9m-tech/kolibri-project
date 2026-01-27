#!/bin/bash
./build/kolibri_node --genome data/large_genome.dat <<EOF
:ask Что такое технология?
:ask Расскажи про науку
:exit
EOF

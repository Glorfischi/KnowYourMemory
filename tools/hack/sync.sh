#!/usr/bin/env bash

PROJ_DIR=${PROJ_DIR:="$HOME/src/github.com/glorfischi/KnowYourMemory"}
REMOTE=${REMOTE:="ault.cscs.ch:~"}

rsync -av $PROJ_DIR $REMOTE

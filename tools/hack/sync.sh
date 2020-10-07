#!/usr/bin/env bash

PROJ_DIR=${PROJ_DIR:="$HOME/src/github.com/glorfischi/KnowYourMemory"}
REMOTE=${REMOTE:="ault.cscs.ch:~"}

rsync -av $PROJ_DIR/src $REMOTE/KnowYourMemory
rsync -av $PROJ_DIR/tests $REMOTE/KnowYourMemory
rsync -av $PROJ_DIR/tools $REMOTE/KnowYourMemory

#!/bin/sh

export PATH=.:$PATH
../udev/runwith ../misc/sockmod ./ifmon

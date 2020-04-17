#!/bin/bash

 # Generate the initial conditions if they are not present.
if [ ! -e EAGLE_ICs_6.hdf5 ]
then
    echo "Fetching initial conditions for the EAGLE 6Mpc example..."
    ./getIC.sh
fi

../../swift --hydro --threads=4 -n 16 -y 1 eagle_6.yml 2>&1 | tee output.log


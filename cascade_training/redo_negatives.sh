#! /bin/bash

# Create list of negative images, randomize the list
# Basically just a copy of the start of prep.sh. 
# Can also be used to add new negatives to the training processes
# while it is running.
/bin/find negative_images -name \*.png > negatives.dat
/bin/find negative_images -name \*.jpg >> negatives.dat
shuf negatives.dat > temp.dat
mv temp.dat negatives.dat

if [ "$#" -eq 1 ]
then
python3 src/upload_images_s3.py $1
elif [ "$#" -eq 2 ]
then
python3 src/upload_images_s3.py $1 $2
elif [ "$#" -eq 3 ]
then
python3 src/upload_images_s3.py $1 $2 $3
fi

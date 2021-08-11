'''
Copyright (c) 2020-2021, NVIDIA CORPORATION. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
'''

import boto3
from botocore.exceptions import ClientError
import logging
import os
from os import environ
import sys
from os import listdir
from os.path import isfile, join

'''

Apis to upload images to S3 bucket.

To use the api's, AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY environmental variables needs to be set
or
~/.aws/config file should be created and include key and secret access key info. A sample config looks like this:

[default]
aws_access_key_id = <your username here>
aws_secret_access_key = <your S3 API key here>

Also, set region, bucket name and endpoint url below to upload to the s3 bucket.
'''

DEFAULT_LOCATION = <region name>
BUCKET_NAME = <name of the bucket to upload images>
ENDPOINT_URL = <endpoint url>

s3 = boto3.client('s3', region_name=DEFAULT_LOCATION, endpoint_url=ENDPOINT_URL)

def get_bucket_list():
    '''
    returns list of buckets.
    '''
    response = s3.list_buckets()
    return [dict['Name'] for dict in response['Buckets']]

def create_bucket(bucket_name):
    '''
    Creates bucket. Versioning is disable by default.
    '''
    response = s3.list_buckets()
    buckets_dict_list = response['Buckets']
    for dict_item in buckets_dict_list:
        if dict_item["Name"] == bucket_name:
            print('bucket with name {} already exists'.format(bucket_name))
            return True

    try:
        location = {'LocationConstraint': DEFAULT_LOCATION}
        s3.create_bucket(Bucket=bucket_name, CreateBucketConfiguration=location)
    except ClientError as e:
        logging.error(e)
        return False

    return True

def get_file_list_in_bucket(bucket_name):
    '''
    Returns list of files in s3 bucket.
    '''
    obj_info = s3.list_objects(Bucket=bucket_name)
    if 'Contents' not in obj_info:
        return []

    return [dict['Key'] for dict in obj_info['Contents']]

def upload_file_to_bucket(bucket_name, file_name, object_name=None):
    '''
    Uploads file to bucket.
    '''
    if object_name is None:
        object_name = os.path.basename(file_name)

    print("uploading ", file_name)
    try:
        response = s3.upload_file(file_name, bucket_name, object_name)
    except ClientError as e:
        logging.error(e)
        return False
    return True

def delete_file_in_bucket(bucket_name, object_name):
    '''
    Deletes file from the bucket.
    '''
    s3.delete_object(Bucket=bucket_name, Key=object_name)

def clear_bucket(bucket_name):
    '''
    Deletes all files from the bucket.
    '''
    s3_res = boto3.resource('s3')
    bucket = s3_res.Bucket(bucket_name)
    bucket.objects.all().delete()

def delete_bucket(bucket_name):
    clear_bucket(bucket_name)
    s3.delete_bucket(Bucket=bucket_name)

if __name__ == '__main__':
    print(sys.argv)
    create_bucket(BUCKET_NAME)
    if upload_file_to_bucket(BUCKET_NAME, sys.argv[1]%int(sys.argv[3])):
        print("Uploading success")
    else:
        print("Uploading failed")
import { Injectable } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import * as AWS from 'aws-sdk';
import { randomUUID } from 'crypto';

@Injectable()
export class StorageService {
  private s3: AWS.S3;
  private bucket: string;

  constructor(private configService: ConfigService) {
    this.s3 = new AWS.S3({
      endpoint: this.configService.get('S3_ENDPOINT'),
      accessKeyId: this.configService.get('S3_ACCESS_KEY'),
      secretAccessKey: this.configService.get('S3_SECRET_KEY'),
      s3ForcePathStyle: true,
      signatureVersion: 'v4',
    });

    this.bucket = this.configService.get('S3_BUCKET', 'social-platform-media');
  }

  /**
   * Upload file to S3
   */
  async uploadFile(
    file: Express.Multer.File,
    folder: string = 'uploads',
  ): Promise<string> {
    const key = `${folder}/${randomUUID()}-${file.originalname}`;

    const params: AWS.S3.PutObjectRequest = {
      Bucket: this.bucket,
      Key: key,
      Body: file.buffer,
      ContentType: file.mimetype,
      ACL: 'public-read',
    };

    await this.s3.putObject(params).promise();

    return `${this.configService.get('S3_ENDPOINT')}/${this.bucket}/${key}`;
  }

  /**
   * Delete file from S3
   */
  async deleteFile(url: string): Promise<void> {
    const key = url.split(`${this.bucket}/`)[1];
    if (!key) {
      return;
    }

    await this.s3
      .deleteObject({
        Bucket: this.bucket,
        Key: key,
      })
      .promise();
  }

  /**
   * Get presigned URL for upload
   */
  async getPresignedUploadUrl(
    filename: string,
    contentType: string,
    folder: string = 'uploads',
  ): Promise<string> {
    const key = `${folder}/${randomUUID()}-${filename}`;

    const params = {
      Bucket: this.bucket,
      Key: key,
      ContentType: contentType,
      Expires: 3600, // 1 hour
    };

    return await this.s3.getSignedUrlPromise('putObject', params);
  }
}

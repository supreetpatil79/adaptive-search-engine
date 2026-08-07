import { Injectable } from '@nestjs/common';

@Injectable()
export class AppService {
  getHello(): string {
    return 'Social Platform API - Three-Network Social Media Application';
  }
}


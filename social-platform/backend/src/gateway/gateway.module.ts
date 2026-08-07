import { Module } from '@nestjs/common';
import { AppGateway } from './gateway.gateway';
import { GatewayService } from './gateway.service';
import { RedisModule } from '../redis/redis.module';
import { ProximityModule } from '../proximity/proximity.module';
import { JwtModule } from '@nestjs/jwt';
import { ConfigModule, ConfigService } from '@nestjs/config';

@Module({
  imports: [
    RedisModule,
    ProximityModule,
    JwtModule.registerAsync({
      imports: [ConfigModule],
      useFactory: (configService: ConfigService) => ({
        secret: configService.get('JWT_SECRET', 'your-secret-key'),
      }),
      inject: [ConfigService],
    }),
  ],
  providers: [AppGateway, GatewayService],
  exports: [AppGateway, GatewayService],
})
export class GatewayModule {}


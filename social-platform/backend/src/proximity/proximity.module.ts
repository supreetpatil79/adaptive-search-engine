import { Module } from '@nestjs/common';
import { TypeOrmModule } from '@nestjs/typeorm';
import { ProximityService } from './proximity.service';
import { ProximityController } from './proximity.controller';
import { ProximitySchedulerService } from './proximity-scheduler.service';
import { ProximitySession } from './entities/proximity-session.entity';
import { Profile } from '../profiles/entities/profile.entity';
import { RedisModule } from '../redis/redis.module';
import { ProfilesModule } from '../profiles/profiles.module';

@Module({
  imports: [
    TypeOrmModule.forFeature([ProximitySession, Profile]),
    RedisModule,
    ProfilesModule,
  ],
  controllers: [ProximityController],
  providers: [ProximityService, ProximitySchedulerService],
  exports: [ProximityService],
})
export class ProximityModule {}


import { Injectable } from '@nestjs/common';
import { Cron, CronExpression } from '@nestjs/schedule';
import { ProximityService } from './proximity.service';

/**
 * Scheduled tasks for proximity detection
 */
@Injectable()
export class ProximitySchedulerService {
  constructor(private proximityService: ProximityService) {}

  /**
   * Check for stale users every 10 seconds
   */
  @Cron(CronExpression.EVERY_10_SECONDS)
  async handleStaleUsers() {
    await this.proximityService.checkStaleUsers();
  }
}

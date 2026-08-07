import {
  Controller,
  Post,
  Get,
  Body,
  UseGuards,
  Request,
  Query,
} from '@nestjs/common';
import { ProximityService } from './proximity.service';
import { JwtAuthGuard } from '../auth/guards/jwt-auth.guard';

interface LocationDto {
  latitude: number;
  longitude: number;
  radius?: number;
}

interface RadarSettingsDto {
  enabled: boolean;
}

@Controller('proximity')
@UseGuards(JwtAuthGuard)
export class ProximityController {
  constructor(private readonly proximityService: ProximityService) {}

  /**
   * Update user location (ping)
   */
  @Post('location')
  async updateLocation(@Request() req, @Body() locationDto: LocationDto) {
    const profileId = req.user.profileId;
    await this.proximityService.updateLocation(
      profileId,
      locationDto.latitude,
      locationDto.longitude,
    );
    return { success: true };
  }

  /**
   * Get nearby users
   */
  @Get('nearby')
  async getNearbyUsers(
    @Request() req,
    @Query('latitude') latitude: number,
    @Query('longitude') longitude: number,
    @Query('radius') radius: number = 50,
  ) {
    const profileId = req.user.profileId;
    const nearby = await this.proximityService.getNearbyUsers(
      profileId,
      parseFloat(latitude.toString()),
      parseFloat(longitude.toString()),
      parseFloat(radius.toString()),
    );
    return { nearby };
  }

  /**
   * Enable/disable radar
   */
  @Post('radar')
  async setRadar(@Request() req, @Body() settings: RadarSettingsDto) {
    const profileId = req.user.profileId;
    await this.proximityService.setRadarEnabled(
      profileId,
      settings.enabled,
    );
    return { success: true, radarEnabled: settings.enabled };
  }

  /**
   * Get radar status
   */
  @Get('radar')
  async getRadarStatus(@Request() req) {
    const profileId = req.user.profileId;
    const enabled = await this.proximityService.isRadarEnabled(profileId);
    return { radarEnabled: enabled };
  }
}


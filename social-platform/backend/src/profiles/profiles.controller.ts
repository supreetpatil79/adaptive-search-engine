import {
  Controller,
  Get,
  Put,
  Post,
  Body,
  Param,
  UseGuards,
  Request,
  Query,
} from '@nestjs/common';
import { ProfilesService } from './profiles.service';
import { JwtAuthGuard } from '../auth/guards/jwt-auth.guard';
import { Profile, ProfileType, Visibility } from './entities/profile.entity';
import { ConnectionType, ConnectionStatus } from './entities/connection.entity';

@Controller('profiles')
@UseGuards(JwtAuthGuard)
export class ProfilesController {
  constructor(private readonly profilesService: ProfilesService) {}

  @Get('me')
  async getMyProfiles(@Request() req) {
    return await this.profilesService.getUserProfiles(req.user.userId);
  }

  @Get(':id')
  async getProfile(@Param('id') id: string, @Request() req) {
    return await this.profilesService.getProfile(id, req.user.profileId);
  }

  @Put(':id')
  async updateProfile(
    @Param('id') id: string,
    @Body() updates: Partial<Profile>,
    @Request() req,
  ) {
    // Verify ownership
    const profile = await this.profilesService.getProfile(id);
    if (profile.userId !== req.user.userId) {
      throw new Error('Not authorized');
    }
    return await this.profilesService.updateProfile(id, updates);
  }

  @Get(':id/views')
  async getProfileViews(
    @Param('id') id: string,
    @Query('networkType') networkType: ProfileType,
    @Request() req,
  ) {
    const profile = await this.profilesService.getProfile(id);
    if (profile.userId !== req.user.userId) {
      throw new Error('Not authorized');
    }
    return await this.profilesService.getProfileViews(id, networkType);
  }

  @Post(':id/connect')
  async createConnection(
    @Param('id') toProfileId: string,
    @Body('connectionType') connectionType: ConnectionType,
    @Request() req,
  ) {
    return await this.profilesService.createConnection(
      req.user.profileId,
      toProfileId,
      connectionType,
    );
  }

  @Post('connections/:id/accept')
  async acceptConnection(@Param('id') connectionId: string, @Request() req) {
    return await this.profilesService.updateConnectionStatus(
      connectionId,
      ConnectionStatus.ACCEPTED,
      req.user.profileId,
    );
  }

  @Post('connections/:id/reject')
  async rejectConnection(@Param('id') connectionId: string, @Request() req) {
    return await this.profilesService.updateConnectionStatus(
      connectionId,
      ConnectionStatus.REJECTED,
      req.user.profileId,
    );
  }

  @Get('connections/requests')
  async getIncomingRequests(@Request() req) {
    return await this.profilesService.getIncomingRequests(req.user.profileId);
  }
}


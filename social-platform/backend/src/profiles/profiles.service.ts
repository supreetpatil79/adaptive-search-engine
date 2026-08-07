import { Injectable, NotFoundException, ForbiddenException } from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { Profile, ProfileType, Visibility } from './entities/profile.entity';
import { Connection, ConnectionType, ConnectionStatus } from './entities/connection.entity';
import { ProfileView } from './entities/profile-view.entity';
import { RedisService } from '../redis/redis.service';

@Injectable()
export class ProfilesService {
  constructor(
    @InjectRepository(Profile)
    private profileRepository: Repository<Profile>,
    @InjectRepository(Connection)
    private connectionRepository: Repository<Connection>,
    @InjectRepository(ProfileView)
    private profileViewRepository: Repository<ProfileView>,
    private redisService: RedisService,
  ) {}

  /**
   * Create default profiles for new user
   */
  async createDefaultProfiles(userId: string): Promise<Profile[]> {
    const profiles = [
      {
        userId,
        type: ProfileType.PROFESSIONAL,
        username: `professional_${userId.substring(0, 8)}`,
        visibility: Visibility.PUBLIC,
      },
      {
        userId,
        type: ProfileType.SOCIAL,
        username: `social_${userId.substring(0, 8)}`,
        visibility: Visibility.PUBLIC,
      },
      {
        userId,
        type: ProfileType.PRIVATE,
        username: `private_${userId.substring(0, 8)}`,
        visibility: Visibility.PRIVATE,
      },
    ];

    const createdProfiles = await Promise.all(
      profiles.map((p) => this.profileRepository.save(this.profileRepository.create(p))),
    );

    // Initialize radar settings
    for (const profile of createdProfiles) {
      await this.redisService.setRadarEnabled(profile.id, false);
    }

    return createdProfiles;
  }

  /**
   * Get all profiles for a user
   */
  async getUserProfiles(userId: string): Promise<Profile[]> {
    return await this.profileRepository.find({
      where: { userId },
      order: { type: 'ASC' },
    });
  }

  /**
   * Get profile by ID with visibility check
   */
  async getProfile(
    profileId: string,
    viewerProfileId?: string,
  ): Promise<Profile> {
    const profile = await this.profileRepository.findOne({
      where: { id: profileId },
      relations: ['user'],
    });

    if (!profile) {
      throw new NotFoundException('Profile not found');
    }

    // Track view (peepin system)
    if (viewerProfileId && viewerProfileId !== profileId) {
      await this.trackProfileView(viewerProfileId, profileId, profile.type);
    }

    // Visibility rules
    if (profile.type === ProfileType.PROFESSIONAL) {
      // Always visible
      return profile;
    }

    if (profile.type === ProfileType.SOCIAL) {
      if (profile.visibility === Visibility.PUBLIC) {
        return profile;
      }
      // Check if viewer is connected
      if (viewerProfileId) {
        const connection = await this.connectionRepository.findOne({
          where: [
            {
              fromProfileId: viewerProfileId,
              toProfileId: profileId,
              connectionType: ConnectionType.FRIEND,
              status: ConnectionStatus.ACCEPTED,
            },
            {
              fromProfileId: profileId,
              toProfileId: viewerProfileId,
              connectionType: ConnectionType.FRIEND,
              status: ConnectionStatus.ACCEPTED,
            },
          ],
        });
        if (connection) {
          return profile;
        }
      }
      throw new ForbiddenException('Profile is private');
    }

    if (profile.type === ProfileType.PRIVATE) {
      // Check if viewer has super private access
      if (viewerProfileId) {
        const connection = await this.connectionRepository.findOne({
          where: [
            {
              fromProfileId: viewerProfileId,
              toProfileId: profileId,
              connectionType: ConnectionType.SUPER_PRIVATE_REQUEST,
              status: ConnectionStatus.ACCEPTED,
            },
            {
              fromProfileId: profileId,
              toProfileId: viewerProfileId,
              connectionType: ConnectionType.SUPER_PRIVATE_REQUEST,
              status: ConnectionStatus.ACCEPTED,
            },
          ],
        });
        if (connection) {
          return profile;
        }
      }
      throw new ForbiddenException('Super private profile requires access');
    }

    return profile;
  }

  /**
   * Update profile
   */
  async updateProfile(
    profileId: string,
    updates: Partial<Profile>,
  ): Promise<Profile> {
    const profile = await this.profileRepository.findOne({
      where: { id: profileId },
    });

    if (!profile) {
      throw new NotFoundException('Profile not found');
    }

    // Professional profiles must remain PUBLIC
    if (profile.type === ProfileType.PROFESSIONAL && updates.visibility) {
      updates.visibility = Visibility.PUBLIC;
    }

    // Private profiles must remain PRIVATE
    if (profile.type === ProfileType.PRIVATE && updates.visibility) {
      updates.visibility = Visibility.PRIVATE;
    }

    Object.assign(profile, updates);
    await this.profileRepository.save(profile);

    // Invalidate cache
    await this.redisService.del(`profile:${profileId}`);

    return profile;
  }

  /**
   * Track profile view (Peepin system)
   */
  async trackProfileView(
    viewerProfileId: string,
    viewedProfileId: string,
    networkType: ProfileType,
  ): Promise<void> {
    // Only track for Professional and Social
    if (networkType === ProfileType.PRIVATE) {
      return;
    }

    // Check if already viewed today
    const today = new Date();
    today.setHours(0, 0, 0, 0);

    const existingView = await this.profileViewRepository.findOne({
      where: {
        viewerProfileId,
        viewedProfileId,
        networkType,
      },
    });

    if (!existingView || existingView.createdAt < today) {
      await this.profileViewRepository.save({
        viewerProfileId,
        viewedProfileId,
        networkType,
      });
    }
  }

  /**
   * Get profile views (Peepin)
   */
  async getProfileViews(profileId: string, networkType: ProfileType): Promise<ProfileView[]> {
    return await this.profileViewRepository.find({
      where: {
        viewedProfileId: profileId,
        networkType,
      },
      relations: ['viewerProfile'],
      order: { createdAt: 'DESC' },
      take: 100,
    });
  }

  /**
   * Create connection request
   */
  async createConnection(
    fromProfileId: string,
    toProfileId: string,
    connectionType: ConnectionType,
  ): Promise<Connection> {
    // Check if connection already exists
    const existing = await this.connectionRepository.findOne({
      where: {
        fromProfileId,
        toProfileId,
        connectionType,
      },
    });

    if (existing) {
      return existing;
    }

    const connection = this.connectionRepository.create({
      fromProfileId,
      toProfileId,
      connectionType,
      status: ConnectionStatus.PENDING,
    });

    return await this.connectionRepository.save(connection);
  }

  /**
   * Accept/reject connection
   */
  async updateConnectionStatus(
    connectionId: string,
    status: ConnectionStatus,
    profileId: string,
  ): Promise<Connection> {
    const connection = await this.connectionRepository.findOne({
      where: { id: connectionId },
    });

    if (!connection) {
      throw new NotFoundException('Connection not found');
    }

    if (connection.toProfileId !== profileId) {
      throw new ForbiddenException('Not authorized');
    }

    connection.status = status;
    return await this.connectionRepository.save(connection);
  }

  /**
   * Get connections
   */
  async getConnections(
    profileId: string,
    type?: ConnectionType,
    status?: ConnectionStatus,
  ): Promise<Connection[]> {
    const where: any = {
      fromProfileId: profileId,
    };

    if (type) {
      where.connectionType = type;
    }

    if (status) {
      where.status = status;
    }

    return await this.connectionRepository.find({
      where,
      relations: ['toProfile'],
    });
  }

  /**
   * Get incoming connection requests
   */
  async getIncomingRequests(profileId: string): Promise<Connection[]> {
    return await this.connectionRepository.find({
      where: {
        toProfileId: profileId,
        status: ConnectionStatus.PENDING,
      },
      relations: ['fromProfile'],
      order: { createdAt: 'DESC' },
    });
  }
}


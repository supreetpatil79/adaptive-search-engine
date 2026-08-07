import { Injectable, UnauthorizedException } from '@nestjs/common';
import { JwtService } from '@nestjs/jwt';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import * as bcrypt from 'bcrypt';
import { User } from '../users/entities/user.entity';
import { RefreshToken } from './entities/refresh-token.entity';
import { UsersService } from '../users/users.service';
import { ProfilesService } from '../profiles/profiles.service';

export interface RegisterDto {
  email: string;
  password: string;
  phone?: string;
}

export interface LoginDto {
  email: string;
  password: string;
}

export interface AuthResponse {
  accessToken: string;
  refreshToken: string;
  user: {
    id: string;
    email: string;
    profiles: Array<{
      id: string;
      type: string;
      username: string;
    }>;
  };
}

@Injectable()
export class AuthService {
  constructor(
    @InjectRepository(User)
    private userRepository: Repository<User>,
    @InjectRepository(RefreshToken)
    private refreshTokenRepository: Repository<RefreshToken>,
    private jwtService: JwtService,
    private usersService: UsersService,
    private profilesService: ProfilesService,
  ) {}

  async register(registerDto: RegisterDto): Promise<AuthResponse> {
    // Check if user exists
    const existingUser = await this.userRepository.findOne({
      where: [{ email: registerDto.email }, { phone: registerDto.phone }],
    });

    if (existingUser) {
      throw new UnauthorizedException('User already exists');
    }

    // Hash password
    const passwordHash = await bcrypt.hash(registerDto.password, 10);

    // Create user
    const user = this.userRepository.create({
      email: registerDto.email,
      phone: registerDto.phone,
      passwordHash,
    });

    await this.userRepository.save(user);

    // Create three profiles (Professional, Social, Private)
    await this.profilesService.createDefaultProfiles(user.id);

    // Generate tokens
    const profiles = await this.profilesService.getUserProfiles(user.id);
    const accessToken = this.generateAccessToken(user.id, profiles[0].id);
    const refreshToken = await this.generateRefreshToken(user.id);

    return {
      accessToken,
      refreshToken,
      user: {
        id: user.id,
        email: user.email,
        profiles: profiles.map((p) => ({
          id: p.id,
          type: p.type,
          username: p.username,
        })),
      },
    };
  }

  async login(loginDto: LoginDto): Promise<AuthResponse> {
    const user = await this.userRepository.findOne({
      where: { email: loginDto.email },
      relations: ['profiles'],
    });

    if (!user) {
      throw new UnauthorizedException('Invalid credentials');
    }

    const isPasswordValid = await bcrypt.compare(
      loginDto.password,
      user.passwordHash,
    );

    if (!isPasswordValid) {
      throw new UnauthorizedException('Invalid credentials');
    }

    // Update last seen
    user.lastSeen = new Date();
    await this.userRepository.save(user);

    // Generate tokens
    const profiles = await this.profilesService.getUserProfiles(user.id);
    const accessToken = this.generateAccessToken(user.id, profiles[0].id);
    const refreshToken = await this.generateRefreshToken(user.id);

    return {
      accessToken,
      refreshToken,
      user: {
        id: user.id,
        email: user.email,
        profiles: profiles.map((p) => ({
          id: p.id,
          type: p.type,
          username: p.username,
        })),
      },
    };
  }

  async refreshAccessToken(refreshToken: string): Promise<{ accessToken: string }> {
    const token = await this.refreshTokenRepository.findOne({
      where: { token: refreshToken },
      relations: ['user'],
    });

    if (!token || token.expiresAt < new Date()) {
      throw new UnauthorizedException('Invalid refresh token');
    }

    const profiles = await this.profilesService.getUserProfiles(token.userId);
    const accessToken = this.generateAccessToken(token.userId, profiles[0].id);

    return { accessToken };
  }

  async validateUser(userId: string): Promise<User | null> {
    return await this.userRepository.findOne({
      where: { id: userId, isActive: true },
    });
  }

  private generateAccessToken(userId: string, profileId: string): string {
    const payload = { sub: userId, profileId };
    return this.jwtService.sign(payload, {
      expiresIn: process.env.JWT_EXPIRES_IN || '7d',
    });
  }

  private async generateRefreshToken(userId: string): Promise<string> {
    const token = this.jwtService.sign(
      { sub: userId, type: 'refresh' },
      { expiresIn: '30d' },
    );

    const expiresAt = new Date();
    expiresAt.setDate(expiresAt.getDate() + 30);

    await this.refreshTokenRepository.save({
      userId,
      token,
      expiresAt,
    });

    return token;
  }

  async logout(refreshToken: string): Promise<void> {
    await this.refreshTokenRepository.delete({ token: refreshToken });
  }
}


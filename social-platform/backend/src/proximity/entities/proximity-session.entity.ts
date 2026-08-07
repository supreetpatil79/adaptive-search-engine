import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  CreateDateColumn,
  ManyToOne,
  JoinColumn,
  Index,
} from 'typeorm';
import { Profile } from '../../profiles/entities/profile.entity';

export enum ProximityStatus {
  ACTIVE = 'ACTIVE',
  GHOSTING = 'GHOSTING',
  OFFLINE = 'OFFLINE',
}

@Entity('proximity_sessions')
@Index(['profile_id'])
@Index(['status'])
export class ProximitySession {
  @PrimaryGeneratedColumn('uuid')
  id: string;

  @Column({ name: 'profile_id' })
  profileId: string;

  @ManyToOne(() => Profile)
  @JoinColumn({ name: 'profile_id' })
  profile: Profile;

  @Column({ type: 'decimal', precision: 10, scale: 8, nullable: true })
  latitude: number;

  @Column({ type: 'decimal', precision: 11, scale: 8, nullable: true })
  longitude: number;

  @Column({
    type: 'varchar',
    length: 20,
    default: ProximityStatus.ACTIVE,
  })
  status: ProximityStatus;

  @Column({ name: 'last_ping', default: () => 'CURRENT_TIMESTAMP' })
  lastPing: Date;

  @CreateDateColumn({ name: 'created_at' })
  createdAt: Date;
}


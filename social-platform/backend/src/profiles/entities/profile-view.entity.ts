import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  CreateDateColumn,
  ManyToOne,
  JoinColumn,
  Index,
} from 'typeorm';
import { Profile } from './profile.entity';
import { ProfileType } from './profile.entity';

@Entity('profile_views')
@Index(['viewer_profile_id', 'viewed_profile_id', 'network_type', 'created_at'])
export class ProfileView {
  @PrimaryGeneratedColumn('uuid')
  id: string;

  @Column({ name: 'viewer_profile_id' })
  viewerProfileId: string;

  @ManyToOne(() => Profile)
  @JoinColumn({ name: 'viewer_profile_id' })
  viewerProfile: Profile;

  @Column({ name: 'viewed_profile_id' })
  viewedProfileId: string;

  @ManyToOne(() => Profile, (profile) => profile.views)
  @JoinColumn({ name: 'viewed_profile_id' })
  viewedProfile: Profile;

  @Column({
    name: 'network_type',
    type: 'varchar',
    length: 20,
  })
  networkType: ProfileType;

  @CreateDateColumn({ name: 'created_at' })
  createdAt: Date;
}


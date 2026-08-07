import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  CreateDateColumn,
  UpdateDateColumn,
  ManyToOne,
  OneToMany,
  JoinColumn,
  Index,
} from 'typeorm';
import { Profile } from '../../profiles/entities/profile.entity';
import { Message } from './message.entity';
import { ProfileType } from '../../profiles/entities/profile.entity';

export enum NetworkType {
  PROFESSIONAL = 'PROFESSIONAL',
  SOCIAL = 'SOCIAL',
  PRIVATE = 'PRIVATE',
}

@Entity('conversations')
@Index(['profile1_id', 'profile2_id', 'network_type'], { unique: true })
export class Conversation {
  @PrimaryGeneratedColumn('uuid')
  id: string;

  @Column({ name: 'profile1_id' })
  profile1Id: string;

  @ManyToOne(() => Profile, (profile) => profile.conversations1)
  @JoinColumn({ name: 'profile1_id' })
  profile1: Profile;

  @Column({ name: 'profile2_id' })
  profile2Id: string;

  @ManyToOne(() => Profile, (profile) => profile.conversations2)
  @JoinColumn({ name: 'profile2_id' })
  profile2: Profile;

  @Column({
    name: 'network_type',
    type: 'varchar',
    length: 20,
  })
  networkType: NetworkType;

  @Column({ name: 'is_encrypted', default: false })
  isEncrypted: boolean;

  @CreateDateColumn({ name: 'created_at' })
  createdAt: Date;

  @UpdateDateColumn({ name: 'updated_at' })
  updatedAt: Date;

  @OneToMany(() => Message, (message) => message.conversation)
  messages: Message[];
}


import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  CreateDateColumn,
  UpdateDateColumn,
  ManyToOne,
  JoinColumn,
  Index,
} from 'typeorm';
import { Profile } from './profile.entity';

export enum ConnectionType {
  FOLLOW = 'FOLLOW',
  FRIEND = 'FRIEND',
  SUPER_PRIVATE_REQUEST = 'SUPER_PRIVATE_REQUEST',
}

export enum ConnectionStatus {
  PENDING = 'PENDING',
  ACCEPTED = 'ACCEPTED',
  REJECTED = 'REJECTED',
  BLOCKED = 'BLOCKED',
}

@Entity('connections')
@Index(['from_profile_id', 'to_profile_id', 'connection_type'], { unique: true })
export class Connection {
  @PrimaryGeneratedColumn('uuid')
  id: string;

  @Column({ name: 'from_profile_id' })
  fromProfileId: string;

  @ManyToOne(() => Profile, (profile) => profile.outgoingConnections)
  @JoinColumn({ name: 'from_profile_id' })
  fromProfile: Profile;

  @Column({ name: 'to_profile_id' })
  toProfileId: string;

  @ManyToOne(() => Profile, (profile) => profile.incomingConnections)
  @JoinColumn({ name: 'to_profile_id' })
  toProfile: Profile;

  @Column({
    name: 'connection_type',
    type: 'varchar',
    length: 20,
  })
  connectionType: ConnectionType;

  @Column({
    type: 'varchar',
    length: 20,
    default: ConnectionStatus.PENDING,
  })
  status: ConnectionStatus;

  @CreateDateColumn({ name: 'created_at' })
  createdAt: Date;

  @UpdateDateColumn({ name: 'updated_at' })
  updatedAt: Date;
}


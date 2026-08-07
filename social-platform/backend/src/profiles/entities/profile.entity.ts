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
import { User } from '../../users/entities/user.entity';
import { Post } from '../../posts/entities/post.entity';
import { Connection } from './connection.entity';
import { ProfileView } from './profile-view.entity';
import { Conversation } from '../../messages/entities/conversation.entity';

export enum ProfileType {
  PROFESSIONAL = 'PROFESSIONAL',
  SOCIAL = 'SOCIAL',
  PRIVATE = 'PRIVATE',
}

export enum Visibility {
  PUBLIC = 'PUBLIC',
  PRIVATE = 'PRIVATE',
}

@Entity('profiles')
@Index(['user_id', 'type'], { unique: true })
@Index(['username', 'type'], { unique: true })
export class Profile {
  @PrimaryGeneratedColumn('uuid')
  id: string;

  @Column({ name: 'user_id' })
  userId: string;

  @ManyToOne(() => User, (user) => user.profiles)
  @JoinColumn({ name: 'user_id' })
  user: User;

  @Column({
    type: 'varchar',
    length: 20,
  })
  type: ProfileType;

  @Column()
  username: string;

  @Column({ name: 'display_name', nullable: true })
  displayName: string;

  @Column({ type: 'text', nullable: true })
  bio: string;

  @Column({ name: 'avatar_url', nullable: true })
  avatarUrl: string;

  @Column({
    type: 'varchar',
    length: 20,
    default: Visibility.PUBLIC,
  })
  visibility: Visibility;

  @CreateDateColumn({ name: 'created_at' })
  createdAt: Date;

  @UpdateDateColumn({ name: 'updated_at' })
  updatedAt: Date;

  @OneToMany(() => Post, (post) => post.profile)
  posts: Post[];

  @OneToMany(() => Connection, (connection) => connection.fromProfile)
  outgoingConnections: Connection[];

  @OneToMany(() => Connection, (connection) => connection.toProfile)
  incomingConnections: Connection[];

  @OneToMany(() => ProfileView, (view) => view.viewedProfile)
  views: ProfileView[];

  @OneToMany(() => Conversation, (conversation) => conversation.profile1)
  conversations1: Conversation[];

  @OneToMany(() => Conversation, (conversation) => conversation.profile2)
  conversations2: Conversation[];
}


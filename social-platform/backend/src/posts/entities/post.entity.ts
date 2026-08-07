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
import { Comment } from './comment.entity';
import { PostLike } from './post-like.entity';

export enum ContentType {
  TEXT = 'TEXT',
  IMAGE = 'IMAGE',
  LINK = 'LINK',
  PROTOTYPE = 'PROTOTYPE',
}

export enum PostVisibility {
  PUBLIC = 'PUBLIC',
  PRIVATE = 'PRIVATE',
  FRIENDS = 'FRIENDS',
}

@Entity('posts')
@Index(['profile_id', 'created_at'])
export class Post {
  @PrimaryGeneratedColumn('uuid')
  id: string;

  @Column({ name: 'profile_id' })
  profileId: string;

  @ManyToOne(() => Profile, (profile) => profile.posts)
  @JoinColumn({ name: 'profile_id' })
  profile: Profile;

  @Column({ type: 'text', nullable: true })
  content: string;

  @Column({
    name: 'content_type',
    type: 'varchar',
    length: 20,
    default: ContentType.TEXT,
  })
  contentType: ContentType;

  @Column({ name: 'media_url', nullable: true })
  mediaUrl: string;

  @Column({ name: 'link_url', nullable: true })
  linkUrl: string;

  @Column({
    type: 'varchar',
    length: 20,
    default: PostVisibility.PUBLIC,
  })
  visibility: PostVisibility;

  @CreateDateColumn({ name: 'created_at' })
  createdAt: Date;

  @UpdateDateColumn({ name: 'updated_at' })
  updatedAt: Date;

  @OneToMany(() => Comment, (comment) => comment.post)
  comments: Comment[];

  @OneToMany(() => PostLike, (like) => like.post)
  likes: PostLike[];
}


import {
  Entity,
  PrimaryGeneratedColumn,
  Column,
  CreateDateColumn,
  ManyToOne,
  JoinColumn,
  Index,
} from 'typeorm';
import { Conversation } from './conversation.entity';
import { Profile } from '../../profiles/entities/profile.entity';

export enum MessageType {
  TEXT = 'TEXT',
  IMAGE = 'IMAGE',
  FILE = 'FILE',
}

@Entity('messages')
@Index(['conversation_id', 'created_at'])
export class Message {
  @PrimaryGeneratedColumn('uuid')
  id: string;

  @Column({ name: 'conversation_id' })
  conversationId: string;

  @ManyToOne(() => Conversation, (conversation) => conversation.messages)
  @JoinColumn({ name: 'conversation_id' })
  conversation: Conversation;

  @Column({ name: 'sender_profile_id' })
  senderProfileId: string;

  @ManyToOne(() => Profile)
  @JoinColumn({ name: 'sender_profile_id' })
  senderProfile: Profile;

  @Column({ type: 'text' })
  content: string;

  @Column({
    name: 'message_type',
    type: 'varchar',
    length: 20,
    default: MessageType.TEXT,
  })
  messageType: MessageType;

  @Column({ name: 'media_url', nullable: true })
  mediaUrl: string;

  @Column({ name: 'is_encrypted', default: false })
  isEncrypted: boolean;

  @CreateDateColumn({ name: 'created_at' })
  createdAt: Date;

  @Column({ name: 'read_at', nullable: true })
  readAt: Date;
}


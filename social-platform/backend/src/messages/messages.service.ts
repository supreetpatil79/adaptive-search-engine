import { Injectable, NotFoundException, ForbiddenException } from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { Conversation, NetworkType } from './entities/conversation.entity';
import { Message, MessageType } from './entities/message.entity';
import { ProfilesService } from '../profiles/profiles.service';
import { ConnectionType, ConnectionStatus } from '../profiles/entities/connection.entity';
import * as crypto from 'crypto';

@Injectable()
export class MessagesService {
  private readonly encryptionKey: string;

  constructor(
    @InjectRepository(Conversation)
    private conversationRepository: Repository<Conversation>,
    @InjectRepository(Message)
    private messageRepository: Repository<Message>,
    private profilesService: ProfilesService,
  ) {
    this.encryptionKey = process.env.ENCRYPTION_KEY || 'default-key-change-in-production';
  }

  /**
   * Get or create conversation
   */
  async getOrCreateConversation(
    profile1Id: string,
    profile2Id: string,
    networkType: NetworkType,
  ): Promise<Conversation> {
    // Ensure profile1Id < profile2Id for consistency
    const [p1, p2] = [profile1Id, profile2Id].sort();

    let conversation = await this.conversationRepository.findOne({
      where: [
        { profile1Id: p1, profile2Id: p2, networkType },
        { profile1Id: p2, profile2Id: p1, networkType },
      ],
    });

    if (!conversation) {
      // Check permissions based on network type
      await this.checkConversationPermission(p1, p2, networkType);

      const isEncrypted = networkType === NetworkType.PRIVATE;

      conversation = this.conversationRepository.create({
        profile1Id: p1,
        profile2Id: p2,
        networkType,
        isEncrypted,
      });

      conversation = await this.conversationRepository.save(conversation);
    }

    return conversation;
  }

  /**
   * Check if conversation is allowed
   */
  private async checkConversationPermission(
    profile1Id: string,
    profile2Id: string,
    networkType: NetworkType,
  ): Promise<void> {
    if (networkType === NetworkType.PRIVATE) {
      // Check if super private connection exists
      const connection = await this.profilesService.getConnections(
        profile1Id,
        ConnectionType.SUPER_PRIVATE_REQUEST,
        ConnectionStatus.ACCEPTED,
      );

      const hasAccess = connection.some(
        (c) => c.toProfileId === profile2Id || c.fromProfileId === profile2Id,
      );

      if (!hasAccess) {
        throw new ForbiddenException('Super private access required');
      }
    }
  }

  /**
   * Send a message
   */
  async sendMessage(
    senderProfileId: string,
    conversationId: string,
    content: string,
    messageType: MessageType = MessageType.TEXT,
    mediaUrl?: string,
  ): Promise<Message> {
    const conversation = await this.conversationRepository.findOne({
      where: { id: conversationId },
    });

    if (!conversation) {
      throw new NotFoundException('Conversation not found');
    }

    if (
      conversation.profile1Id !== senderProfileId &&
      conversation.profile2Id !== senderProfileId
    ) {
      throw new ForbiddenException('Not authorized');
    }

    // Encrypt if super private
    let encryptedContent = content;
    let isEncrypted = false;

    if (conversation.isEncrypted) {
      encryptedContent = this.encryptMessage(content);
      isEncrypted = true;
    }

    const message = this.messageRepository.create({
      conversationId,
      senderProfileId,
      content: encryptedContent,
      messageType,
      mediaUrl,
      isEncrypted,
    });

    return await this.messageRepository.save(message);
  }

  /**
   * Get messages for a conversation
   */
  async getMessages(
    conversationId: string,
    profileId: string,
  ): Promise<Message[]> {
    const conversation = await this.conversationRepository.findOne({
      where: { id: conversationId },
    });

    if (!conversation) {
      throw new NotFoundException('Conversation not found');
    }

    if (
      conversation.profile1Id !== profileId &&
      conversation.profile2Id !== profileId
    ) {
      throw new ForbiddenException('Not authorized');
    }

    const messages = await this.messageRepository.find({
      where: { conversationId },
      relations: ['senderProfile'],
      order: { createdAt: 'ASC' },
    });

    // Decrypt messages if needed
    if (conversation.isEncrypted) {
      messages.forEach((msg) => {
        if (msg.isEncrypted) {
          msg.content = this.decryptMessage(msg.content);
        }
      });
    }

    return messages;
  }

  /**
   * Get conversations for a profile
   */
  async getConversations(profileId: string): Promise<Conversation[]> {
    return await this.conversationRepository.find({
      where: [{ profile1Id: profileId }, { profile2Id: profileId }],
      relations: ['profile1', 'profile2'],
      order: { updatedAt: 'DESC' },
    });
  }

  /**
   * Mark message as read
   */
  async markAsRead(messageId: string, profileId: string): Promise<void> {
    const message = await this.messageRepository.findOne({
      where: { id: messageId },
      relations: ['conversation'],
    });

    if (!message) {
      throw new NotFoundException('Message not found');
    }

    if (
      message.conversation.profile1Id !== profileId &&
      message.conversation.profile2Id !== profileId
    ) {
      throw new ForbiddenException('Not authorized');
    }

    if (message.senderProfileId !== profileId) {
      message.readAt = new Date();
      await this.messageRepository.save(message);
    }
  }

  /**
   * Encrypt message (AES-256-GCM)
   */
  private encryptMessage(plaintext: string): string {
    const iv = crypto.randomBytes(16);
    const cipher = crypto.createCipheriv(
      'aes-256-gcm',
      Buffer.from(this.encryptionKey.substring(0, 32), 'utf8'),
      iv,
    );

    let encrypted = cipher.update(plaintext, 'utf8', 'hex');
    encrypted += cipher.final('hex');

    const authTag = cipher.getAuthTag();

    return `${iv.toString('hex')}:${authTag.toString('hex')}:${encrypted}`;
  }

  /**
   * Decrypt message
   */
  private decryptMessage(ciphertext: string): string {
    const parts = ciphertext.split(':');
    if (parts.length !== 3) {
      throw new Error('Invalid encrypted message format');
    }

    const iv = Buffer.from(parts[0], 'hex');
    const authTag = Buffer.from(parts[1], 'hex');
    const encrypted = parts[2];

    const decipher = crypto.createDecipheriv(
      'aes-256-gcm',
      Buffer.from(this.encryptionKey.substring(0, 32), 'utf8'),
      iv,
    );

    decipher.setAuthTag(authTag);

    let decrypted = decipher.update(encrypted, 'hex', 'utf8');
    decrypted += decipher.final('utf8');

    return decrypted;
  }
}


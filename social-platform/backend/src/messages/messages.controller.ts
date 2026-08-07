import {
  Controller,
  Get,
  Post,
  Body,
  Param,
  UseGuards,
  Request,
} from '@nestjs/common';
import { MessagesService } from './messages.service';
import { JwtAuthGuard } from '../auth/guards/jwt-auth.guard';
import { NetworkType } from './entities/conversation.entity';
import { MessageType } from './entities/message.entity';

@Controller('messages')
@UseGuards(JwtAuthGuard)
export class MessagesController {
  constructor(private readonly messagesService: MessagesService) {}

  @Get('conversations')
  async getConversations(@Request() req) {
    return await this.messagesService.getConversations(req.user.profileId);
  }

  @Post('conversations')
  async createConversation(
    @Body('profileId') profileId: string,
    @Body('networkType') networkType: NetworkType,
    @Request() req,
  ) {
    return await this.messagesService.getOrCreateConversation(
      req.user.profileId,
      profileId,
      networkType,
    );
  }

  @Get('conversations/:id/messages')
  async getMessages(@Param('id') conversationId: string, @Request() req) {
    return await this.messagesService.getMessages(
      conversationId,
      req.user.profileId,
    );
  }

  @Post('conversations/:id/messages')
  async sendMessage(
    @Param('id') conversationId: string,
    @Body('content') content: string,
    @Body('messageType') messageType: MessageType,
    @Body('mediaUrl') mediaUrl: string,
    @Request() req,
  ) {
    return await this.messagesService.sendMessage(
      req.user.profileId,
      conversationId,
      content,
      messageType,
      mediaUrl,
    );
  }

  @Post('messages/:id/read')
  async markAsRead(@Param('id') messageId: string, @Request() req) {
    await this.messagesService.markAsRead(messageId, req.user.profileId);
    return { success: true };
  }
}


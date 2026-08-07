import {
  WebSocketGateway,
  WebSocketServer,
  SubscribeMessage,
  OnGatewayConnection,
  OnGatewayDisconnect,
  ConnectedSocket,
  MessageBody,
} from '@nestjs/websockets';
import { Server, Socket } from 'socket.io';
import { JwtService } from '@nestjs/jwt';
import { RedisService } from '../redis/redis.service';
import { ProximityService } from '../proximity/proximity.service';
import { GatewayService } from './gateway.service';

interface AuthenticatedSocket extends Socket {
  userId?: string;
  profileId?: string;
}

@WebSocketGateway({
  cors: {
    origin: process.env.FRONTEND_URL || 'http://localhost:3000',
    credentials: true,
  },
  namespace: '/',
})
export class AppGateway
  implements OnGatewayConnection, OnGatewayDisconnect
{
  @WebSocketServer()
  server: Server;

  constructor(
    private jwtService: JwtService,
    private redisService: RedisService,
    private proximityService: ProximityService,
    private gatewayService: GatewayService,
  ) {}

  async handleConnection(client: AuthenticatedSocket) {
    try {
      // Authenticate via token
      const token = client.handshake.auth.token || client.handshake.headers.authorization?.replace('Bearer ', '');
      
      if (!token) {
        client.disconnect();
        return;
      }

      const payload = this.jwtService.verify(token);
      client.userId = payload.sub;
      client.profileId = payload.profileId;

      // Register WebSocket session
      await this.redisService.setWebSocketSession(client.id, client.profileId);
      await this.redisService.addOnlineUser(client.profileId);

      // Join user's personal room
      client.join(`user:${client.profileId}`);

      console.log(`Client connected: ${client.profileId}`);
    } catch (error) {
      console.error('WebSocket authentication failed:', error);
      client.disconnect();
    }
  }

  async handleDisconnect(client: AuthenticatedSocket) {
    if (client.profileId) {
      await this.redisService.removeWebSocketSession(client.id);
      await this.redisService.removeOnlineUser(client.profileId);
      
      // Handle user leaving proximity
      await this.proximityService.handleUserLeft(client.profileId);
      
      console.log(`Client disconnected: ${client.profileId}`);
    }
  }

  /**
   * Handle location updates
   */
  @SubscribeMessage('location:update')
  async handleLocationUpdate(
    @ConnectedSocket() client: AuthenticatedSocket,
    @MessageBody() data: { latitude: number; longitude: number },
  ) {
    if (!client.profileId) {
      return;
    }

    await this.proximityService.updateLocation(
      client.profileId,
      data.latitude,
      data.longitude,
    );

    // Get nearby users and emit to client
    const nearby = await this.proximityService.getNearbyUsers(
      client.profileId,
      data.latitude,
      data.longitude,
    );

    client.emit('proximity:nearby', { nearby });

    // Notify nearby users about this user
    for (const user of nearby) {
      if (user.status === 'ACTIVE') {
        this.server.to(`user:${user.profileId}`).emit('proximity:user-entered', {
          profileId: client.profileId,
          distance: user.distance,
        });
      }
    }
  }

  /**
   * Handle new message
   */
  @SubscribeMessage('message:send')
  async handleMessage(
    @ConnectedSocket() client: AuthenticatedSocket,
    @MessageBody() data: { conversationId: string; content: string },
  ) {
    if (!client.profileId) {
      return;
    }

    // Emit to conversation participants
    this.server.to(`conversation:${data.conversationId}`).emit('message:new', {
      conversationId: data.conversationId,
      senderProfileId: client.profileId,
      content: data.content,
      timestamp: new Date(),
    });
  }

  /**
   * Join conversation room
   */
  @SubscribeMessage('conversation:join')
  async handleJoinConversation(
    @ConnectedSocket() client: AuthenticatedSocket,
    @MessageBody() data: { conversationId: string },
  ) {
    client.join(`conversation:${data.conversationId}`);
  }

  /**
   * Leave conversation room
   */
  @SubscribeMessage('conversation:leave')
  async handleLeaveConversation(
    @ConnectedSocket() client: AuthenticatedSocket,
    @MessageBody() data: { conversationId: string },
  ) {
    client.leave(`conversation:${data.conversationId}`);
  }

  /**
   * Handle radar toggle
   */
  @SubscribeMessage('radar:toggle')
  async handleRadarToggle(
    @ConnectedSocket() client: AuthenticatedSocket,
    @MessageBody() data: { enabled: boolean },
  ) {
    if (!client.profileId) {
      return;
    }

    await this.proximityService.setRadarEnabled(client.profileId, data.enabled);
    client.emit('radar:status', { enabled: data.enabled });
  }

  /**
   * Emit to specific user (helper method)
   */
  async emitToUser(profileId: string, event: string, data: any): Promise<void> {
    this.server.to(`user:${profileId}`).emit(event, data);
  }

  /**
   * Broadcast to all connected clients
   */
  async broadcast(event: string, data: any): Promise<void> {
    this.server.emit(event, data);
  }
}

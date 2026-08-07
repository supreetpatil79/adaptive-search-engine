import {
  Controller,
  Get,
  Post,
  Delete,
  Body,
  Param,
  UseGuards,
  Request,
} from '@nestjs/common';
import { PostsService } from './posts.service';
import { JwtAuthGuard } from '../auth/guards/jwt-auth.guard';
import { Post as PostEntity } from './entities/post.entity';

@Controller('posts')
@UseGuards(JwtAuthGuard)
export class PostsController {
  constructor(private readonly postsService: PostsService) {}

  @Post()
  async createPost(@Body() postData: Partial<PostEntity>, @Request() req) {
    return await this.postsService.createPost(req.user.profileId, postData);
  }

  @Get('feed')
  async getFeed(@Request() req) {
    return await this.postsService.getFeed(req.user.profileId);
  }

  @Get('profile/:profileId')
  async getProfilePosts(@Param('profileId') profileId: string, @Request() req) {
    return await this.postsService.getProfilePosts(
      profileId,
      req.user.profileId,
    );
  }

  @Post(':id/like')
  async likePost(@Param('id') postId: string, @Request() req) {
    return await this.postsService.likePost(req.user.profileId, postId);
  }

  @Post(':id/unlike')
  async unlikePost(@Param('id') postId: string, @Request() req) {
    await this.postsService.unlikePost(req.user.profileId, postId);
    return { success: true };
  }

  @Post(':id/comment')
  async addComment(
    @Param('id') postId: string,
    @Body('content') content: string,
    @Request() req,
  ) {
    return await this.postsService.addComment(
      req.user.profileId,
      postId,
      content,
    );
  }

  @Get(':id/comments')
  async getComments(@Param('id') postId: string) {
    return await this.postsService.getComments(postId);
  }

  @Delete(':id')
  async deletePost(@Param('id') postId: string, @Request() req) {
    await this.postsService.deletePost(postId, req.user.profileId);
    return { success: true };
  }
}


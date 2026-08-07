import { Injectable, NotFoundException, ForbiddenException } from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository, In } from 'typeorm';
import { Post, ContentType, PostVisibility } from './entities/post.entity';
import { Comment } from './entities/comment.entity';
import { PostLike } from './entities/post-like.entity';
import { ProfilesService } from '../profiles/profiles.service';
import { RedisService } from '../redis/redis.service';

@Injectable()
export class PostsService {
  constructor(
    @InjectRepository(Post)
    private postRepository: Repository<Post>,
    @InjectRepository(Comment)
    private commentRepository: Repository<Comment>,
    @InjectRepository(PostLike)
    private postLikeRepository: Repository<PostLike>,
    private profilesService: ProfilesService,
    private redisService: RedisService,
  ) {}

  /**
   * Create a new post
   */
  async createPost(
    profileId: string,
    postData: Partial<Post>,
  ): Promise<Post> {
    const post = this.postRepository.create({
      profileId,
      ...postData,
    });

    const savedPost = await this.postRepository.save(post);

    // Invalidate feed cache
    await this.invalidateFeedCache(profileId);

    return savedPost;
  }

  /**
   * Get posts for a profile
   */
  async getProfilePosts(
    profileId: string,
    viewerProfileId?: string,
  ): Promise<Post[]> {
    const profile = await this.profilesService.getProfile(
      profileId,
      viewerProfileId,
    );

    if (!profile) {
      throw new NotFoundException('Profile not found');
    }

    return await this.postRepository.find({
      where: { profileId },
      relations: ['profile', 'likes', 'comments'],
      order: { createdAt: 'DESC' },
    });
  }

  /**
   * Get feed (posts from followed profiles)
   */
  async getFeed(profileId: string): Promise<Post[]> {
    // Check cache
    const cached = await this.redisService.getCachedFeed(profileId);
    if (cached) {
      return cached;
    }

    // Get connections
    const connections = await this.profilesService.getConnections(
      profileId,
      undefined,
      undefined,
    );

    const connectedProfileIds = connections
      .filter((c) => c.status === 'ACCEPTED')
      .map((c) => c.toProfileId);

    if (connectedProfileIds.length === 0) {
      return [];
    }

    const posts = await this.postRepository.find({
      where: { profileId: In(connectedProfileIds) },
      relations: ['profile', 'likes', 'comments'],
      order: { createdAt: 'DESC' },
      take: 50,
    });

    // Cache feed
    await this.redisService.cacheFeed(profileId, posts);

    return posts;
  }

  /**
   * Like a post
   */
  async likePost(profileId: string, postId: string): Promise<PostLike> {
    const existingLike = await this.postLikeRepository.findOne({
      where: { postId, profileId },
    });

    if (existingLike) {
      return existingLike;
    }

    const like = this.postLikeRepository.create({
      postId,
      profileId,
    });

    return await this.postLikeRepository.save(like);
  }

  /**
   * Unlike a post
   */
  async unlikePost(profileId: string, postId: string): Promise<void> {
    await this.postLikeRepository.delete({ postId, profileId });
  }

  /**
   * Add comment
   */
  async addComment(
    profileId: string,
    postId: string,
    content: string,
  ): Promise<Comment> {
    const comment = this.commentRepository.create({
      postId,
      profileId,
      content,
    });

    return await this.commentRepository.save(comment);
  }

  /**
   * Get comments for a post
   */
  async getComments(postId: string): Promise<Comment[]> {
    return await this.commentRepository.find({
      where: { postId },
      relations: ['profile'],
      order: { createdAt: 'ASC' },
    });
  }

  /**
   * Delete post
   */
  async deletePost(postId: string, profileId: string): Promise<void> {
    const post = await this.postRepository.findOne({
      where: { id: postId },
    });

    if (!post) {
      throw new NotFoundException('Post not found');
    }

    if (post.profileId !== profileId) {
      throw new ForbiddenException('Not authorized');
    }

    await this.postRepository.delete(postId);
    await this.invalidateFeedCache(profileId);
  }

  private async invalidateFeedCache(profileId: string): Promise<void> {
    await this.redisService.del(`feed:${profileId}`);
  }
}


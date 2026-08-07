'use client';

import { useEffect, useState, useRef } from 'react';
import { motion, AnimatePresence } from 'framer-motion';
import { getSocket } from '@/lib/websocket';

interface NearbyUser {
  profileId: string;
  distance: number;
  status: 'ACTIVE' | 'GHOSTING' | 'OFFLINE';
  ghostTime?: number;
  profile?: {
    id: string;
    username: string;
    displayName: string;
    avatarUrl: string;
    type: string;
    visibility: string;
  };
}

interface RadarProps {
  radius?: number;
  onUserClick?: (user: NearbyUser) => void;
}

export default function Radar({ radius = 200, onUserClick }: RadarProps) {
  const [nearbyUsers, setNearbyUsers] = useState<Map<string, NearbyUser>>(new Map());
  const [ghostTimers, setGhostTimers] = useState<Map<string, number>>(new Map());
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const animationFrameRef = useRef<number>();

  useEffect(() => {
    const socket = getSocket();
    if (!socket) return;

    const handleNearby = (data: { nearby: NearbyUser[] }) => {
      const newMap = new Map(nearbyUsers);
      const newTimers = new Map(ghostTimers);

      // Update existing users and add new ones
      data.nearby.forEach((user) => {
        if (user.status === 'GHOSTING' && user.ghostTime) {
          newTimers.set(user.profileId, user.ghostTime);
        }
        newMap.set(user.profileId, user);
      });

      // Remove users not in the list (they've gone offline)
      const currentIds = new Set(data.nearby.map((u) => u.profileId));
      newMap.forEach((_, id) => {
        if (!currentIds.has(id)) {
          newMap.delete(id);
          newTimers.delete(id);
        }
      });

      setNearbyUsers(newMap);
      setGhostTimers(newTimers);
    };

    const handleUserEntered = (data: { profileId: string; distance: number }) => {
      // User entered proximity - will be included in next nearby update
    };

    socket.on('proximity:nearby', handleNearby);
    socket.on('proximity:user-entered', handleUserEntered);

    return () => {
      socket.off('proximity:nearby', handleNearby);
      socket.off('proximity:user-entered', handleUserEntered);
    };
  }, []);

  // Animation loop for ghost mode
  useEffect(() => {
    const animate = () => {
      const now = Date.now();
      const updatedUsers = new Map(nearbyUsers);

      ghostTimers.forEach((ghostStart, profileId) => {
        const user = updatedUsers.get(profileId);
        if (user && user.status === 'GHOSTING') {
          const elapsed = (now - ghostStart) / 1000; // seconds
          if (elapsed >= 60) {
            // Remove after 60 seconds
            updatedUsers.delete(profileId);
            setNearbyUsers(updatedUsers);
            setGhostTimers((prev) => {
              const next = new Map(prev);
              next.delete(profileId);
              return next;
            });
          }
        }
      });

      animationFrameRef.current = requestAnimationFrame(animate);
    };

    animationFrameRef.current = requestAnimationFrame(animate);
    return () => {
      if (animationFrameRef.current) {
        cancelAnimationFrame(animationFrameRef.current);
      }
    };
  }, [nearbyUsers, ghostTimers]);

  // Convert user to polar coordinates
  const getUserPosition = (user: NearbyUser, index: number) => {
    const maxDistance = 50; // meters
    const normalizedDistance = Math.min(user.distance / maxDistance, 1);
    const r = (radius * 0.8) * normalizedDistance; // 80% of radar radius

    // Use hash of profileId for stable angle
    const hash = user.profileId.split('').reduce((acc, char) => {
      return ((acc << 5) - acc) + char.charCodeAt(0);
    }, 0);
    const angle = (hash % 360) * (Math.PI / 180);

    const x = r * Math.cos(angle);
    const y = r * Math.sin(angle);

    return { x, y, r, angle };
  };

  // Calculate ghost animation properties
  const getGhostProperties = (user: NearbyUser) => {
    if (user.status !== 'GHOSTING' || !user.ghostTime) {
      return { opacity: 1, scale: 1, offsetX: 0, offsetY: 0 };
    }

    const elapsed = (Date.now() - user.ghostTime) / 1000;
    const progress = Math.min(elapsed / 60, 1);

    return {
      opacity: 1 - progress * 0.7, // Fade to 30% opacity
      scale: 1 + progress * 0.2, // Slight scale up
      offsetX: progress * radius * 0.3 * Math.cos(getUserPosition(user, 0).angle),
      offsetY: progress * radius * 0.3 * Math.sin(getUserPosition(user, 0).angle),
    };
  };

  return (
    <div className="relative w-full h-full flex items-center justify-center">
      {/* Radar Circle Background */}
      <div
        className="absolute rounded-full border-4 border-primary-500/30"
        style={{
          width: radius * 2,
          height: radius * 2,
          background: 'radial-gradient(circle, rgba(14,165,233,0.1) 0%, transparent 70%)',
        }}
      >
        {/* Radar Sweep Animation */}
        <motion.div
          className="absolute inset-0 rounded-full"
          style={{
            background: 'conic-gradient(from 0deg, transparent 0deg, rgba(14,165,233,0.3) 45deg, transparent 90deg)',
          }}
          animate={{ rotate: 360 }}
          transition={{
            duration: 3,
            repeat: Infinity,
            ease: 'linear',
          }}
        />
      </div>

      {/* Center Dot (You) */}
      <div className="absolute z-10 w-4 h-4 bg-primary-500 rounded-full border-2 border-white shadow-lg" />

      {/* Nearby Users */}
      <AnimatePresence>
        {Array.from(nearbyUsers.values()).map((user) => {
          if (!user.profile) return null;

          const pos = getUserPosition(user, 0);
          const ghostProps = getGhostProperties(user);
          const isGhosting = user.status === 'GHOSTING';

          return (
            <motion.div
              key={user.profileId}
              className="absolute cursor-pointer z-20"
              style={{
                left: `calc(50% + ${pos.x + ghostProps.offsetX}px)`,
                top: `calc(50% + ${pos.y + ghostProps.offsetY}px)`,
                transform: 'translate(-50%, -50%)',
              }}
              initial={{ opacity: 0, scale: 0 }}
              animate={{
                opacity: ghostProps.opacity,
                scale: ghostProps.scale,
              }}
              exit={{ opacity: 0, scale: 0 }}
              transition={{ duration: 0.3 }}
              onClick={() => onUserClick?.(user)}
            >
              {/* User Avatar */}
              <div
                className={`w-12 h-12 rounded-full border-2 ${
                  isGhosting
                    ? 'border-gray-400 opacity-50'
                    : 'border-primary-500 shadow-lg'
                } overflow-hidden bg-white`}
              >
                {user.profile.avatarUrl ? (
                  <img
                    src={user.profile.avatarUrl}
                    alt={user.profile.displayName}
                    className="w-full h-full object-cover"
                  />
                ) : (
                  <div className="w-full h-full flex items-center justify-center bg-gray-200 text-gray-600 text-xs font-bold">
                    {user.profile.displayName.charAt(0).toUpperCase()}
                  </div>
                )}
              </div>

              {/* Distance Badge */}
              <div
                className={`absolute -bottom-6 left-1/2 transform -translate-x-1/2 text-xs px-2 py-1 rounded ${
                  isGhosting
                    ? 'bg-gray-400/50 text-gray-600'
                    : 'bg-primary-500 text-white'
                }`}
              >
                {Math.round(user.distance)}m
              </div>

              {/* Pulsing Ring for Active Users */}
              {!isGhosting && (
                <motion.div
                  className="absolute inset-0 rounded-full border-2 border-primary-500"
                  animate={{
                    scale: [1, 1.5, 1],
                    opacity: [0.5, 0, 0.5],
                  }}
                  transition={{
                    duration: 2,
                    repeat: Infinity,
                    ease: 'easeInOut',
                  }}
                />
              )}
            </motion.div>
          );
        })}
      </AnimatePresence>

      {/* Legend */}
      <div className="absolute bottom-4 left-4 bg-white/90 backdrop-blur-sm rounded-lg p-3 shadow-lg text-xs">
        <div className="flex items-center gap-2 mb-1">
          <div className="w-3 h-3 rounded-full bg-primary-500" />
          <span>Active ({Array.from(nearbyUsers.values()).filter((u) => u.status === 'ACTIVE').length})</span>
        </div>
        <div className="flex items-center gap-2">
          <div className="w-3 h-3 rounded-full bg-gray-400 opacity-50" />
          <span>Ghosting ({Array.from(nearbyUsers.values()).filter((u) => u.status === 'GHOSTING').length})</span>
        </div>
      </div>
    </div>
  );
}

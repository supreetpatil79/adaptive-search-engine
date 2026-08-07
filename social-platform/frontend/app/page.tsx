'use client';

import { useEffect, useState } from 'react';
import { useRouter } from 'next/navigation';
import { useAuthStore } from '@/store/auth-store';
import { connectWebSocket, disconnectWebSocket, getSocket } from '@/lib/websocket';
import NetworkTabs, { NetworkType } from '@/components/NetworkTabs';
import Radar from '@/components/Radar';
import RadarToggle from '@/components/RadarToggle';
import api from '@/lib/api';

export default function Home() {
  const router = useRouter();
  const { user, accessToken, logout } = useAuthStore();
  const [activeTab, setActiveTab] = useState<NetworkType>('PROFESSIONAL');
  const [showRadar, setShowRadar] = useState(false);
  const [locationPermission, setLocationPermission] = useState<'granted' | 'denied' | 'prompt'>('prompt');

  useEffect(() => {
    if (!user || !accessToken) {
      router.push('/login');
      return;
    }

    // Connect WebSocket
    const socket = connectWebSocket(accessToken);

    // Request location permission
    if ('geolocation' in navigator) {
      navigator.geolocation.getCurrentPosition(
        () => setLocationPermission('granted'),
        () => setLocationPermission('denied'),
      );
    }

    // Start location pings if permission granted
    let locationInterval: NodeJS.Timeout;
    if (locationPermission === 'granted') {
      locationInterval = setInterval(() => {
        navigator.geolocation.getCurrentPosition(
          (position) => {
            socket.emit('location:update', {
              latitude: position.coords.latitude,
              longitude: position.coords.longitude,
            });
          },
          (error) => console.error('Location error:', error),
        );
      }, 10000); // Every 10 seconds
    }

    return () => {
      if (locationInterval) clearInterval(locationInterval);
      disconnectWebSocket();
    };
  }, [user, accessToken, locationPermission]);

  const handleUserClick = (user: any) => {
    // Navigate to profile view
    router.push(`/profiles/${user.profileId}`);
  };

  return (
    <div className="min-h-screen bg-gray-50">
      {/* Header */}
      <header className="bg-white shadow-sm border-b">
        <div className="max-w-7xl mx-auto px-4 py-3 flex items-center justify-between">
          <h1 className="text-xl font-bold text-gray-900">Social Platform</h1>
          <div className="flex items-center gap-4">
            <RadarToggle />
            <button
              onClick={() => setShowRadar(!showRadar)}
              className="px-4 py-2 bg-gray-100 rounded-lg hover:bg-gray-200"
            >
              {showRadar ? 'Hide Radar' : 'Show Radar'}
            </button>
            <button
              onClick={logout}
              className="px-4 py-2 text-gray-600 hover:text-gray-900"
            >
              Logout
            </button>
          </div>
        </div>
      </header>

      {/* Network Tabs */}
      <NetworkTabs activeTab={activeTab} onTabChange={setActiveTab} />

      {/* Main Content */}
      <div className="max-w-7xl mx-auto px-4 py-6">
        {showRadar && (
          <div className="mb-6 bg-white rounded-lg shadow p-6">
            <h2 className="text-lg font-semibold mb-4">Proximity Radar</h2>
            {locationPermission === 'granted' ? (
              <div className="h-96 relative">
                <Radar radius={180} onUserClick={handleUserClick} />
              </div>
            ) : (
              <div className="text-center py-12">
                <p className="text-gray-600 mb-4">
                  Location permission is required for radar mode.
                </p>
                <button
                  onClick={() => {
                    navigator.geolocation.getCurrentPosition(
                      () => setLocationPermission('granted'),
                      () => setLocationPermission('denied'),
                    );
                  }}
                  className="px-4 py-2 bg-primary-500 text-white rounded-lg hover:bg-primary-600"
                >
                  Enable Location
                </button>
              </div>
            )}
          </div>
        )}

        {/* Network Content */}
        <div className="bg-white rounded-lg shadow p-6">
          <h2 className="text-xl font-semibold mb-4">
            {activeTab === 'PROFESSIONAL' && '💼 Professional Network'}
            {activeTab === 'SOCIAL' && '📱 Social Network'}
            {activeTab === 'PRIVATE' && '🔒 Super Private Network'}
          </h2>
          <p className="text-gray-600">
            Content for {activeTab.toLowerCase()} network will be displayed here.
          </p>
        </div>
      </div>
    </div>
  );
}

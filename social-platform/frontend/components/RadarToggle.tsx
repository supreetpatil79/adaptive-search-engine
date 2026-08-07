'use client';

import { useState, useEffect } from 'react';
import { getSocket } from '@/lib/websocket';
import api from '@/lib/api';

export default function RadarToggle() {
  const [enabled, setEnabled] = useState(false);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    // Fetch initial radar status
    api
      .get('/proximity/radar')
      .then((res) => setEnabled(res.data.radarEnabled))
      .catch(console.error);
  }, []);

  const handleToggle = async () => {
    setLoading(true);
    try {
      const socket = getSocket();
      if (socket) {
        socket.emit('radar:toggle', { enabled: !enabled });
      }

      const res = await api.post('/proximity/radar', { enabled: !enabled });
      setEnabled(res.data.radarEnabled);
    } catch (error) {
      console.error('Failed to toggle radar:', error);
    } finally {
      setLoading(false);
    }
  };

  return (
    <button
      onClick={handleToggle}
      disabled={loading}
      className={`px-4 py-2 rounded-lg font-medium transition-colors ${
        enabled
          ? 'bg-primary-500 text-white hover:bg-primary-600'
          : 'bg-gray-200 text-gray-700 hover:bg-gray-300'
      }`}
    >
      {enabled ? '📍 Radar ON' : '📍 Radar OFF'}
    </button>
  );
}

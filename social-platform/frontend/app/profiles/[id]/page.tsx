'use client';

import { useEffect, useState } from 'react';
import { useParams, useRouter } from 'next/navigation';
import api from '@/lib/api';
import { useAuthStore } from '@/store/auth-store';

export default function ProfilePage() {
  const params = useParams();
  const router = useRouter();
  const { user } = useAuthStore();
  const [profile, setProfile] = useState<any>(null);
  const [loading, setLoading] = useState(true);
  const [connectionType, setConnectionType] = useState<string>('');

  useEffect(() => {
    if (params.id) {
      api
        .get(`/profiles/${params.id}`)
        .then((res) => {
          setProfile(res.data);
        })
        .catch((err) => {
          console.error('Failed to load profile:', err);
          if (err.response?.status === 403) {
            alert('This profile is private. You need to request access.');
          }
        })
        .finally(() => setLoading(false));
    }
  }, [params.id]);

  const handleConnect = async (type: string) => {
    try {
      await api.post(`/profiles/${params.id}/connect`, { connectionType: type });
      alert('Request sent!');
    } catch (error: any) {
      alert(error.response?.data?.message || 'Failed to send request');
    }
  };

  if (loading) {
    return <div className="p-8 text-center">Loading...</div>;
  }

  if (!profile) {
    return <div className="p-8 text-center">Profile not found</div>;
  }

  return (
    <div className="max-w-4xl mx-auto px-4 py-8">
      <button
        onClick={() => router.back()}
        className="mb-4 text-primary-500 hover:text-primary-600"
      >
        ← Back
      </button>

      <div className="bg-white rounded-lg shadow p-6">
        <div className="flex items-start gap-6">
          {profile.avatarUrl && (
            <img
              src={profile.avatarUrl}
              alt={profile.displayName}
              className="w-24 h-24 rounded-full"
            />
          )}
          <div className="flex-1">
            <h1 className="text-2xl font-bold">{profile.displayName || profile.username}</h1>
            <p className="text-gray-600">@{profile.username}</p>
            <p className="mt-2 text-gray-700">{profile.bio}</p>
            <div className="mt-4 flex gap-2">
              <span className="px-3 py-1 bg-gray-100 rounded-full text-sm">
                {profile.type}
              </span>
              <span className="px-3 py-1 bg-gray-100 rounded-full text-sm">
                {profile.visibility}
              </span>
            </div>
          </div>
        </div>

        {profile.type === 'PRIVATE' && (
          <div className="mt-6 p-4 bg-yellow-50 border border-yellow-200 rounded-lg">
            <p className="text-sm text-yellow-800">
              This is a Super Private profile. Request access to view and message.
            </p>
            <button
              onClick={() => handleConnect('SUPER_PRIVATE_REQUEST')}
              className="mt-2 px-4 py-2 bg-primary-500 text-white rounded-lg hover:bg-primary-600"
            >
              Request Super Private Access
            </button>
          </div>
        )}

        {profile.type !== 'PRIVATE' && (
          <div className="mt-6 flex gap-2">
            <button
              onClick={() => handleConnect('FOLLOW')}
              className="px-4 py-2 bg-primary-500 text-white rounded-lg hover:bg-primary-600"
            >
              Follow
            </button>
            {profile.type === 'SOCIAL' && (
              <button
                onClick={() => handleConnect('FRIEND')}
                className="px-4 py-2 bg-gray-200 text-gray-700 rounded-lg hover:bg-gray-300"
              >
                Send Friend Request
              </button>
            )}
          </div>
        )}
      </div>
    </div>
  );
}

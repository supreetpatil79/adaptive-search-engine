'use client';

import { useState } from 'react';

export type NetworkType = 'PROFESSIONAL' | 'SOCIAL' | 'PRIVATE';

interface NetworkTabsProps {
  activeTab: NetworkType;
  onTabChange: (tab: NetworkType) => void;
}

export default function NetworkTabs({ activeTab, onTabChange }: NetworkTabsProps) {
  const tabs: { id: NetworkType; label: string; icon: string }[] = [
    { id: 'PROFESSIONAL', label: 'Professional', icon: '💼' },
    { id: 'SOCIAL', label: 'Social', icon: '📱' },
    { id: 'PRIVATE', label: 'Super Private', icon: '🔒' },
  ];

  return (
    <div className="flex border-b border-gray-200 bg-white">
      {tabs.map((tab) => (
        <button
          key={tab.id}
          onClick={() => onTabChange(tab.id)}
          className={`flex-1 px-4 py-3 text-sm font-medium transition-colors ${
            activeTab === tab.id
              ? 'border-b-2 border-primary-500 text-primary-600'
              : 'text-gray-600 hover:text-gray-900'
          }`}
        >
          <span className="mr-2">{tab.icon}</span>
          {tab.label}
        </button>
      ))}
    </div>
  );
}

import React from 'react';
import {
  View,
  Text,
  StyleSheet,
  ScrollView,
  TouchableOpacity,
  Image,
  Alert,
} from 'react-native';

interface License {
  id: string;
  tier: 'Startup' | 'Business' | 'Enterprise';
  expiryDate: string;
  status: 'active' | 'expiring' | 'expired';
  users: number;
  maxUsers: number;
  storage: string;
}

interface DashboardScreenProps {
  navigation: any;
}

export default function DashboardScreen({ navigation }: DashboardScreenProps) {
  const [licenses, setLicenses] = React.useState<License[]>([
    {
      id: 'LIC-001',
      tier: 'Startup',
      expiryDate: '2026-11-12',
      status: 'active',
      users: 3,
      maxUsers: 10,
      storage: '20 GB',
    },
  ]);

  const stats = {
    activeLicenses: licenses.filter(l => l.status === 'active').length,
    totalUsers: licenses.reduce((sum, l) => sum + l.users, 0),
    storageUsed: '5 GB',
    nextRenewal: '2026-11-12',
  };

  return (
    <ScrollView style={styles.container} showsVerticalScrollIndicator={false}>
      {/* Header */}
      <View style={styles.header}>
        <Text style={styles.greeting}>Добро пожаловать!</Text>
        <TouchableOpacity
          onPress={() => navigation.navigate('Settings')}
          style={styles.settingsButton}
        >
          <Text style={styles.settingsIcon}>⚙️</Text>
        </TouchableOpacity>
      </View>

      {/* Stats Cards */}
      <View style={styles.statsGrid}>
        <View style={styles.statCard}>
          <Text style={styles.statNumber}>{stats.activeLicenses}</Text>
          <Text style={styles.statLabel}>Активных лицензий</Text>
        </View>
        <View style={styles.statCard}>
          <Text style={styles.statNumber}>{stats.totalUsers}</Text>
          <Text style={styles.statLabel}>Пользователей</Text>
        </View>
        <View style={styles.statCard}>
          <Text style={styles.statNumber}>{stats.storageUsed}</Text>
          <Text style={styles.statLabel}>Использовано</Text>
        </View>
        <View style={styles.statCard}>
          <Text style={styles.statNumber}>30 дн</Text>
          <Text style={styles.statLabel}>До обновления</Text>
        </View>
      </View>

      {/* Quick Actions */}
      <View style={styles.section}>
        <Text style={styles.sectionTitle}>Быстрые действия</Text>
        <View style={styles.actionsRow}>
          <TouchableOpacity
            style={styles.actionButton}
            onPress={() => navigation.navigate('Licenses')}
          >
            <Text style={styles.actionIcon}>📋</Text>
            <Text style={styles.actionLabel}>Лицензии</Text>
          </TouchableOpacity>
          <TouchableOpacity
            style={styles.actionButton}
            onPress={() => navigation.navigate('Payments')}
          >
            <Text style={styles.actionIcon}>💳</Text>
            <Text style={styles.actionLabel}>Платежи</Text>
          </TouchableOpacity>
          <TouchableOpacity
            style={styles.actionButton}
            onPress={() => Alert.alert('Поддержка', 'Свяжитесь с нами')}
          >
            <Text style={styles.actionIcon}>💬</Text>
            <Text style={styles.actionLabel}>Поддержка</Text>
          </TouchableOpacity>
          <TouchableOpacity
            style={styles.actionButton}
            onPress={() => Alert.alert('QR код', 'Создание QR кода')}
          >
            <Text style={styles.actionIcon}>📱</Text>
            <Text style={styles.actionLabel}>QR код</Text>
          </TouchableOpacity>
        </View>
      </View>

      {/* Active Licenses */}
      <View style={styles.section}>
        <View style={styles.sectionHeader}>
          <Text style={styles.sectionTitle}>Мои лицензии</Text>
          <TouchableOpacity onPress={() => navigation.navigate('Licenses')}>
            <Text style={styles.seeAll}>Все →</Text>
          </TouchableOpacity>
        </View>

        {licenses.map(license => (
          <TouchableOpacity
            key={license.id}
            style={styles.licenseCard}
            onPress={() => navigation.navigate('LicenseDetail', { licenseId: license.id })}
          >
            <View style={styles.licenseHeader}>
              <View>
                <Text style={styles.licenseId}>{license.id}</Text>
                <Text style={styles.licenseTier}>{license.tier} План</Text>
              </View>
              <View
                style={[
                  styles.statusBadge,
                  license.status === 'active' && styles.statusActive,
                  license.status === 'expiring' && styles.statusExpiring,
                  license.status === 'expired' && styles.statusExpired,
                ]}
              >
                <Text style={styles.statusText}>
                  {license.status === 'active' && 'Активна'}
                  {license.status === 'expiring' && 'Истекает'}
                  {license.status === 'expired' && 'Истекла'}
                </Text>
              </View>
            </View>

            <View style={styles.licenseBody}>
              <View style={styles.licenseInfo}>
                <Text style={styles.infoLabel}>Пользователи</Text>
                <Text style={styles.infoValue}>
                  {license.users} / {license.maxUsers}
                </Text>
                <View style={styles.progressBar}>
                  <View
                    style={[
                      styles.progressFill,
                      { width: `${(license.users / license.maxUsers) * 100}%` },
                    ]}
                  />
                </View>
              </View>

              <View style={styles.licenseInfo}>
                <Text style={styles.infoLabel}>Хранилище</Text>
                <Text style={styles.infoValue}>{license.storage}</Text>
              </View>

              <View style={styles.licenseInfo}>
                <Text style={styles.infoLabel}>Истекает</Text>
                <Text style={styles.infoValue}>{license.expiryDate}</Text>
              </View>
            </View>

            <TouchableOpacity
              style={styles.renewButton}
              onPress={() => Alert.alert('Продление', 'Перейти в платежи?')}
            >
              <Text style={styles.renewButtonText}>Продлить →</Text>
            </TouchableOpacity>
          </TouchableOpacity>
        ))}
      </View>

      {/* News/Updates */}
      <View style={styles.section}>
        <Text style={styles.sectionTitle}>Новости</Text>
        <View style={styles.newsCard}>
          <Text style={styles.newsDate}>12 ноября 2025</Text>
          <Text style={styles.newsTitle}>Kolibri v1.1.0 выпущена! 🚀</Text>
          <Text style={styles.newsDescription}>
            Новая версия с поддержкой файлов до 1 GB и динамической аллокацией буферов.
          </Text>
          <TouchableOpacity>
            <Text style={styles.newsLink}>Подробнее →</Text>
          </TouchableOpacity>
        </View>
      </View>

      <View style={styles.footer} />
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#0f0f0f',
  },
  header: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    padding: 20,
    paddingTop: 10,
  },
  greeting: {
    fontSize: 24,
    fontWeight: 'bold',
    color: '#fff',
  },
  settingsButton: {
    padding: 8,
  },
  settingsIcon: {
    fontSize: 24,
  },
  statsGrid: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    paddingHorizontal: 10,
    paddingBottom: 10,
  },
  statCard: {
    width: '50%',
    padding: 10,
  },
  statCardInner: {
    backgroundColor: '#1a1a2e',
    borderRadius: 12,
    padding: 16,
    alignItems: 'center',
    borderLeftWidth: 4,
    borderLeftColor: '#00d4ff',
  },
  statNumber: {
    fontSize: 28,
    fontWeight: 'bold',
    color: '#00d4ff',
    marginBottom: 4,
  },
  statLabel: {
    fontSize: 12,
    color: '#999',
    textAlign: 'center',
  },
  section: {
    paddingHorizontal: 20,
    marginBottom: 30,
  },
  sectionHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 16,
  },
  sectionTitle: {
    fontSize: 18,
    fontWeight: 'bold',
    color: '#fff',
  },
  seeAll: {
    color: '#00d4ff',
    fontSize: 14,
  },
  actionsRow: {
    flexDirection: 'row',
    justifyContent: 'space-between',
  },
  actionButton: {
    alignItems: 'center',
    padding: 12,
    backgroundColor: '#1a1a2e',
    borderRadius: 12,
    flex: 1,
    marginHorizontal: 4,
  },
  actionIcon: {
    fontSize: 28,
    marginBottom: 8,
  },
  actionLabel: {
    fontSize: 11,
    color: '#999',
    textAlign: 'center',
  },
  licenseCard: {
    backgroundColor: '#1a1a2e',
    borderRadius: 12,
    padding: 16,
    marginBottom: 12,
    borderLeftWidth: 4,
    borderLeftColor: '#00d4ff',
  },
  licenseHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    marginBottom: 12,
  },
  licenseId: {
    fontSize: 14,
    fontWeight: 'bold',
    color: '#fff',
  },
  licenseTier: {
    fontSize: 12,
    color: '#00d4ff',
    marginTop: 4,
  },
  statusBadge: {
    paddingHorizontal: 12,
    paddingVertical: 6,
    borderRadius: 20,
  },
  statusActive: {
    backgroundColor: '#00d4ff20',
  },
  statusExpiring: {
    backgroundColor: '#ffaa0020',
  },
  statusExpired: {
    backgroundColor: '#ff444420',
  },
  statusText: {
    fontSize: 12,
    fontWeight: '600',
    color: '#fff',
  },
  licenseBody: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    marginBottom: 12,
  },
  licenseInfo: {
    flex: 1,
  },
  infoLabel: {
    fontSize: 11,
    color: '#666',
    marginBottom: 4,
  },
  infoValue: {
    fontSize: 14,
    fontWeight: 'bold',
    color: '#fff',
  },
  progressBar: {
    height: 4,
    backgroundColor: '#2a2a3e',
    borderRadius: 2,
    marginTop: 4,
    overflow: 'hidden',
  },
  progressFill: {
    height: '100%',
    backgroundColor: '#00d4ff',
  },
  renewButton: {
    backgroundColor: '#00d4ff20',
    borderRadius: 8,
    padding: 8,
    alignItems: 'center',
  },
  renewButtonText: {
    color: '#00d4ff',
    fontSize: 12,
    fontWeight: '600',
  },
  newsCard: {
    backgroundColor: '#1a1a2e',
    borderRadius: 12,
    padding: 16,
  },
  newsDate: {
    fontSize: 11,
    color: '#666',
    marginBottom: 4,
  },
  newsTitle: {
    fontSize: 16,
    fontWeight: 'bold',
    color: '#fff',
    marginBottom: 8,
  },
  newsDescription: {
    fontSize: 13,
    color: '#999',
    lineHeight: 20,
    marginBottom: 12,
  },
  newsLink: {
    color: '#00d4ff',
    fontSize: 12,
    fontWeight: '600',
  },
  footer: {
    height: 20,
  },
});

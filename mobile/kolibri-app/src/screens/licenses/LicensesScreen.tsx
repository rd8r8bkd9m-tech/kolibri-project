import React, { useState } from 'react';
import {
  View,
  Text,
  StyleSheet,
  ScrollView,
  TouchableOpacity,
  Alert,
} from 'react-native';

interface License {
  id: string;
  tier: string;
  status: string;
  expiryDate: string;
  users: number;
  maxUsers: number;
}

interface LicensesScreenProps {
  navigation: any;
}

export default function LicensesScreen({ navigation }: LicensesScreenProps) {
  const [licenses] = useState<License[]>([
    {
      id: 'LIC-001',
      tier: 'Startup',
      status: 'active',
      expiryDate: '2026-11-12',
      users: 3,
      maxUsers: 10,
    },
  ]);

  return (
    <ScrollView style={styles.container}>
      <View style={styles.header}>
        <Text style={styles.title}>Мои лицензии ({licenses.length})</Text>
      </View>

      {licenses.map(license => (
        <TouchableOpacity
          key={license.id}
          style={styles.licenseCard}
          onPress={() => navigation.navigate('LicenseDetail', { licenseId: license.id })}
        >
          <Text style={styles.licenseId}>{license.id}</Text>
          <Text style={styles.licenseTier}>{license.tier} План</Text>
          <Text style={styles.licenseStatus}>{license.status}</Text>
          <Text style={styles.expiryDate}>Истекает: {license.expiryDate}</Text>
          <View style={styles.usersInfo}>
            <Text style={styles.users}>Пользователи: {license.users} / {license.maxUsers}</Text>
          </View>
        </TouchableOpacity>
      ))}

      <TouchableOpacity
        style={styles.addButton}
        onPress={() => Alert.alert('Новая лицензия', 'Функция в разработке')}
      >
        <Text style={styles.addButtonText}>+ Добавить лицензию</Text>
      </TouchableOpacity>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#0f0f0f',
    padding: 20,
  },
  header: {
    marginBottom: 20,
  },
  title: {
    fontSize: 24,
    fontWeight: 'bold',
    color: '#fff',
  },
  licenseCard: {
    backgroundColor: '#1a1a2e',
    borderRadius: 12,
    padding: 16,
    marginBottom: 12,
    borderLeftWidth: 4,
    borderLeftColor: '#00d4ff',
  },
  licenseId: {
    fontSize: 16,
    fontWeight: 'bold',
    color: '#fff',
  },
  licenseTier: {
    fontSize: 14,
    color: '#00d4ff',
    marginTop: 8,
  },
  licenseStatus: {
    fontSize: 12,
    color: '#00d4ff',
    marginTop: 8,
  },
  expiryDate: {
    fontSize: 12,
    color: '#999',
    marginTop: 8,
  },
  usersInfo: {
    marginTop: 12,
  },
  users: {
    fontSize: 12,
    color: '#999',
  },
  addButton: {
    backgroundColor: '#00d4ff20',
    borderWidth: 2,
    borderColor: '#00d4ff',
    borderRadius: 8,
    padding: 16,
    alignItems: 'center',
    marginTop: 20,
  },
  addButtonText: {
    color: '#00d4ff',
    fontSize: 16,
    fontWeight: 'bold',
  },
});

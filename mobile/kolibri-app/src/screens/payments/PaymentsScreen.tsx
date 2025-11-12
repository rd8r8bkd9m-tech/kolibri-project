import React from 'react';
import { View, Text, StyleSheet, ScrollView, TouchableOpacity } from 'react-native';

interface PaymentsScreenProps {
  navigation: any;
}

export default function PaymentsScreen({ navigation }: PaymentsScreenProps) {
  const payments = [
    {
      id: 'PAY-001',
      date: '10.11.2025',
      amount: '$10,000',
      method: 'Яндекс.Касса',
      status: 'completed',
      license: 'LIC-001',
    },
    {
      id: 'PAY-002',
      date: '05.11.2025',
      amount: '$50,000',
      method: 'Sberbank',
      status: 'completed',
      license: 'LIC-002',
    },
  ];

  return (
    <ScrollView style={styles.container}>
      <View style={styles.header}>
        <Text style={styles.title}>Платежи</Text>
        <View style={styles.balanceCard}>
          <Text style={styles.balanceLabel}>Баланс счета</Text>
          <Text style={styles.balanceAmount}>$2,350.00</Text>
        </View>
      </View>

      <View style={styles.section}>
        <Text style={styles.sectionTitle}>Недавние платежи</Text>
        {payments.map(payment => (
          <View key={payment.id} style={styles.paymentItem}>
            <View style={styles.paymentInfo}>
              <Text style={styles.paymentDate}>{payment.date}</Text>
              <Text style={styles.paymentMethod}>{payment.method}</Text>
            </View>
            <View style={styles.paymentAmount}>
              <Text style={styles.paymentAmountValue}>{payment.amount}</Text>
              <View
                style={[
                  styles.statusBadge,
                  payment.status === 'completed' && styles.statusCompleted,
                ]}
              >
                <Text style={styles.statusText}>✓ Выполнено</Text>
              </View>
            </View>
          </View>
        ))}
      </View>

      <TouchableOpacity style={styles.payButton}>
        <Text style={styles.payButtonText}>Произвести платёж</Text>
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
    marginBottom: 30,
  },
  title: {
    fontSize: 24,
    fontWeight: 'bold',
    color: '#fff',
    marginBottom: 16,
  },
  balanceCard: {
    backgroundColor: '#1a1a2e',
    borderRadius: 12,
    padding: 20,
    borderLeftWidth: 4,
    borderLeftColor: '#00d4ff',
  },
  balanceLabel: {
    fontSize: 14,
    color: '#999',
    marginBottom: 8,
  },
  balanceAmount: {
    fontSize: 28,
    fontWeight: 'bold',
    color: '#00d4ff',
  },
  section: {
    marginBottom: 30,
  },
  sectionTitle: {
    fontSize: 18,
    fontWeight: 'bold',
    color: '#fff',
    marginBottom: 16,
  },
  paymentItem: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    backgroundColor: '#1a1a2e',
    borderRadius: 8,
    padding: 12,
    marginBottom: 12,
  },
  paymentInfo: {
    flex: 1,
  },
  paymentDate: {
    fontSize: 14,
    fontWeight: 'bold',
    color: '#fff',
  },
  paymentMethod: {
    fontSize: 12,
    color: '#999',
    marginTop: 4,
  },
  paymentAmount: {
    alignItems: 'flex-end',
  },
  paymentAmountValue: {
    fontSize: 16,
    fontWeight: 'bold',
    color: '#fff',
  },
  statusBadge: {
    marginTop: 4,
    paddingHorizontal: 8,
    paddingVertical: 4,
    borderRadius: 4,
  },
  statusCompleted: {
    backgroundColor: '#00d4ff20',
  },
  statusText: {
    fontSize: 11,
    color: '#00d4ff',
    fontWeight: '600',
  },
  payButton: {
    backgroundColor: '#00d4ff',
    borderRadius: 8,
    padding: 16,
    alignItems: 'center',
    marginBottom: 20,
  },
  payButtonText: {
    color: '#000',
    fontSize: 16,
    fontWeight: 'bold',
  },
});

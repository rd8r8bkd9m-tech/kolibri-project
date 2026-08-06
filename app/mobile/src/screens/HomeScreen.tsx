import React from 'react';
import { View, StyleSheet, ScrollView } from 'react-native';
import { Card, Title, Paragraph, Button } from 'react-native-paper';

export default function HomeScreen({ navigation }) {
  return (
    <ScrollView style={styles.container}>
      <View style={styles.header}>
        <Title style={styles.title}>Kolibri Smeta</Title>
        <Paragraph>Расчет смет ФЕР/ГЭСН/ТЕР</Paragraph>
      </View>

      <Card style={styles.card}>
        <Card.Content>
          <Title>Создать смету</Title>
          <Paragraph>Создайте новую смету вручную или с помощью AI</Paragraph>
        </Card.Content>
        <Card.Actions>
          <Button onPress={() => navigation.navigate('Smeta')}>Перейти</Button>
        </Card.Actions>
      </Card>

      <Card style={styles.card}>
        <Card.Content>
          <Title>Калькулятор объемов</Title>
          <Paragraph>Рассчитайте объемы работ по размерам помещений</Paragraph>
        </Card.Content>
        <Card.Actions>
          <Button onPress={() => navigation.navigate('Calculator')}>Перейти</Button>
        </Card.Actions>
      </Card>

      <Card style={styles.card}>
        <Card.Content>
          <Title>Возможности</Title>
          <Paragraph>
            ✓ Автоматический расчет по ФЕР/ГЭСН/ТЕР{'\n'}
            ✓ AI распознавание видов работ{'\n'}
            ✓ Работа в оффлайн-режиме{'\n'}
            ✓ Экспорт в PDF/Excel/Word{'\n'}
            ✓ Региональные коэффициенты
          </Paragraph>
        </Card.Content>
      </Card>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#f5f5f5',
  },
  header: {
    padding: 20,
    backgroundColor: '#fff',
    marginBottom: 10,
  },
  title: {
    fontSize: 28,
    fontWeight: 'bold',
  },
  card: {
    margin: 10,
  },
});

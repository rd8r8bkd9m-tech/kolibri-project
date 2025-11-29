import React from 'react';
import { View, StyleSheet, ScrollView } from 'react-native';
import { List, Switch } from 'react-native-paper';

export default function SettingsScreen() {
  const [offlineMode, setOfflineMode] = React.useState(false);

  return (
    <ScrollView style={styles.container}>
      <List.Section>
        <List.Subheader>Общие настройки</List.Subheader>
        <List.Item
          title="Оффлайн режим"
          description="Работа без интернета"
          right={() => <Switch value={offlineMode} onValueChange={setOfflineMode} />}
        />
        <List.Item
          title="Регион"
          description="Москва"
          left={props => <List.Icon {...props} icon="map-marker" />}
          onPress={() => {}}
        />
      </List.Section>

      <List.Section>
        <List.Subheader>Коэффициенты</List.Subheader>
        <List.Item
          title="Региональный"
          description="1.15"
          left={props => <List.Icon {...props} icon="chart-line" />}
          onPress={() => {}}
        />
        <List.Item
          title="Сезонный"
          description="1.00"
          left={props => <List.Icon {...props} icon="weather-sunny" />}
          onPress={() => {}}
        />
      </List.Section>

      <List.Section>
        <List.Subheader>Синхронизация</List.Subheader>
        <List.Item
          title="Синхронизировать"
          description="Последняя: 23.11.2024"
          left={props => <List.Icon {...props} icon="sync" />}
          onPress={() => {}}
        />
      </List.Section>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#f5f5f5',
  },
});

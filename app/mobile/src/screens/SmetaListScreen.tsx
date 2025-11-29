import React from 'react';
import { View, StyleSheet, ScrollView } from 'react-native';
import { List, FAB } from 'react-native-paper';

export default function SmetaListScreen({ navigation }) {
  return (
    <View style={styles.container}>
      <ScrollView>
        <List.Item
          title="Смета №1 - Штукатурка"
          description="20.11.2024 • 45,000 руб"
          left={props => <List.Icon {...props} icon="document-text" />}
          onPress={() => {}}
        />
        <List.Item
          title="Смета №2 - Покраска"
          description="19.11.2024 • 32,500 руб"
          left={props => <List.Icon {...props} icon="document-text" />}
          onPress={() => {}}
        />
      </ScrollView>
      <FAB
        style={styles.fab}
        icon="plus"
        onPress={() => {}}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#f5f5f5',
  },
  fab: {
    position: 'absolute',
    margin: 16,
    right: 0,
    bottom: 0,
  },
});

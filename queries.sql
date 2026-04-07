-- 1. High-earning Engineering or Operations staff in major hubs
SELECT name, city, department, salary 
FROM users 
WHERE (department = 'Engineering' OR department = 'Operations') AND (city = 'Bangalore' OR city = 'Mumbai') AND salary > 80000;

-- 2. Senior Management or Finance leaders who are currently Active
SELECT name, experience_years, department, status 
FROM users 
WHERE experience_years > 15 AND (department = 'Management' OR department = 'Finance') AND status = 'Active';

-- 3. Non-Active high earners OR specific city budget alerts
SELECT name, city, status, salary 
FROM users 
WHERE (status != 'Active' AND salary > 90000) OR (city = 'Delhi' AND salary < 70000);

-- 4. Deeply nested logic: Young high-earners OR Veterans with moderate pay, all must be Active
SELECT name, age, salary, status 
FROM users 
WHERE ((age < 30 AND salary > 75000) OR (age > 40 AND salary < 100000)) AND status = 'Active';

-- 5. Multiple ORs for regional Sales/Marketing focus
SELECT name, city, department 
FROM users 
WHERE (department = 'Marketing' OR department = 'Sales') AND (city = 'Pune' OR city = 'Mumbai' OR city = 'Chennai' OR city = 'Hyderabad');
